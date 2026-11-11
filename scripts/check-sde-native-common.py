#!/usr/bin/env python3
"""Shared host-only runner for the extracted SDE native DMA source checks."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys

REPO = Path(__file__).resolve().parents[1]
BO_FLAGS = ("MSM_BO_STOLEN", "MSM_BO_KEEPATTRS", "MSM_BO_EXTBUF")
DOMAIN_NAMES = ("MSM_SMMU_DOMAIN_UNSECURE", "MSM_SMMU_DOMAIN_NRT_UNSECURE",
                "MSM_SMMU_DOMAIN_SECURE", "MSM_SMMU_DOMAIN_NRT_SECURE",
                "MSM_SMMU_DOMAIN_MAX")
ACTIVE_ARGS = None


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def arguments(description, joint=False):
    global ACTIVE_ARGS
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("--source", required=True, type=Path,
                        help="prepared source root or its techpack/display/msm directory")
    parser.add_argument("--output", required=True, type=Path,
                        help="new result directory; existing evidence is never overwritten")
    parser.add_argument("--cc", default="gcc", help="host C compiler executable (default: gcc)")
    parser.add_argument("--sanitize", default="undefined",
                        choices=("undefined", "address,undefined", "none"))
    parser.add_argument("--timeout", default=60, type=int,
                        help="per compile/run timeout in seconds")
    parser.add_argument("--reference-result", type=Path,
                        help="optional previous RESULT.json; compare literal test stdout/stderr/exit")
    if joint:
        parser.add_argument("--vma-source", type=Path,
                            help="explicit successor msm_gem_vma.c; defaults to --source's file")
    args = parser.parse_args()
    args.source = args.source.resolve()
    msm = args.source / "techpack/display/msm"
    if not msm.is_dir():
        msm = args.source
    if not (msm / "msm_mmu.h").is_file():
        parser.error("--source must contain msm_mmu.h or techpack/display/msm/msm_mmu.h")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    args.msm = msm
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    args.inputs = {}
    args.fixtures = {}
    ACTIVE_ARGS = args
    if joint:
        args.vma_source = (args.vma_source or msm / "msm_gem_vma.c").resolve()
    return args


def entry(main):
    try:
        return main()
    except (ValueError, OSError) as error:
        print(f"input error: {error}", file=sys.stderr)
        if ACTIVE_ARGS is not None:
            result = dict(status="fail", phase="input-or-comparison", error=str(error),
                          inputs=ACTIVE_ARGS.inputs, fixtures=ACTIVE_ARGS.fixtures,
                          hardware_validated=False)
            (ACTIVE_ARGS.output / "RESULT.json").write_text(json.dumps(result, indent=2) + "\n")
        return 2


def extract(text, name, kind="function"):
    """Return exact source text while ignoring braces inside C comments/strings."""
    if kind == "struct":
        pattern = r"struct\s+" + re.escape(name) + r"\s*\{"
    else:
        pattern = (r"(?:static\s+)?(?:int|void|bool|struct device\s*\*)\s*" +
                   re.escape(name) + r"\s*\([^;]*?\)\s*\{")
    match = re.search(pattern, text, re.S)
    if not match:
        raise ValueError(f"source declaration not found: {name}")
    opening = text.index("{", match.start())
    depth, state, i = 1, "code", opening + 1
    while i < len(text) and depth:
        char, pair = text[i], text[i:i + 2]
        if state == "line":
            if char == "\n":
                state = "code"
        elif state == "comment":
            if pair == "*/":
                state = "code"
                i += 1
        elif state in ('"', "'"):
            if char == "\\":
                i += 1
            elif char == state:
                state = "code"
        elif pair == "//":
            state = "line"
            i += 1
        elif pair == "/*":
            state = "comment"
            i += 1
        elif char in ('"', "'"):
            state = char
        else:
            depth += (char == "{") - (char == "}")
        i += 1
    if depth:
        raise ValueError(f"unterminated source declaration: {name}")
    return text[match.start():i] + (";" if kind == "struct" else "")


def read_input(args, path):
    path = Path(path).resolve()
    args.inputs[str(path)] = sha(path)
    return path.read_text(encoding="utf-8")


def fixture(args, name):
    path = REPO / "tests" / name
    args.fixtures[str(path.resolve())] = sha(path)
    return path.read_text(encoding="utf-8")


def source_constants(args, prefix):
    """Use the target BO macro tokens and enum, not copied numerical test flags.

    Only simple integer BO definitions are accepted deliberately: an unfamiliar
    expression must be reviewed, not silently replaced with an assumed mask.
    The domain enum's exact declared order is required by these modeled arrays.
    """
    gem = read_input(args, args.msm / "msm_gem.h")
    uncommented = re.sub(r"/\*.*?\*/|//[^\n]*", "", gem, flags=re.S)
    values = {}
    for name in BO_FLAGS:
        match = re.search(r"^[ \t]*#define[ \t]+" + name +
                          r"[ \t]+((?:0[xX][0-9a-fA-F]+|[0-9]+)[uUlL]*)[ \t]*$",
                          uncommented, re.M)
        if not match:
            raise ValueError(f"missing or unsupported literal BO flag: {name}")
        literal = match.group(1)
        numeric = int(re.sub(r"[uUlL]+$", "", literal), 0)
        if not numeric or numeric & (numeric - 1):
            raise ValueError(f"BO flag is not one bit: {name}={literal}")
        values[name] = {"literal": literal, "value": numeric}
        prefix = re.sub(r"^\s*#define\s+" + name + r"\s+[^\n]+$",
                        "", prefix, flags=re.M)
        prefix = f"#define {name} {literal}\n" + prefix
    if len({v["value"] for v in values.values()}) != len(values):
        raise ValueError("BO flag aliases would invalidate the ownership test inputs")
    header = read_input(args, args.msm / "msm_mmu.h")
    match = re.search(r"enum\s+msm_mmu_domain_type\s*\{([^}]+)\}\s*;", header, re.S)
    if not match:
        raise ValueError("missing msm_mmu_domain_type")
    enum_body = re.sub(r"/\*.*?\*/|//[^\n]*", "", match.group(1), flags=re.S)
    entries = tuple(x.strip() for x in enum_body.split(",") if x.strip())
    if entries != DOMAIN_NAMES:
        raise ValueError("domain enum layout changed; review host fixture before running")
    actual_enum = match.group(0)
    if "enum msm_mmu_domain_type" in prefix:
        prefix = re.sub(r"enum\s+msm_mmu_domain_type\s*\{[^}]+\}\s*;",
                        actual_enum, prefix, flags=re.S)
    else:
        prefix = re.sub(r"^#define MSM_SMMU_DOMAIN_(?:MAX|UNSECURE) [^\n]+\n",
                        "", prefix, flags=re.M)
        prefix += "\n" + actual_enum + "\n"
    args.constants = {"bo_flags": values, "domain_enum": actual_enum}
    return prefix


def run_harness(args, harness, warning_exceptions=(), functions=(), native_only=0):
    path = args.output / "harness.c"
    path.write_text(harness, encoding="utf-8")
    binary = args.output / "check"
    command = [args.cc, "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror"]
    command.extend("-Wno-" + flag for flag in warning_exceptions)
    if args.sanitize != "none":
        command += ["-fsanitize=" + args.sanitize, "-fno-omit-frame-pointer"]
        if "address" in args.sanitize:
            command += ["-fno-pie", "-no-pie"]
    command += [str(path), "-o", str(binary)]
    runs = []
    for label, cmd in (("compile", command), ("run", [str(binary)])):
        try:
            result = subprocess.run(cmd, capture_output=True, text=True,
                                    timeout=args.timeout,
                                    env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"})
            stdout, stderr, code = result.stdout, result.stderr, result.returncode
        except FileNotFoundError as error:
            stdout, stderr, code = "", str(error), 127
        except subprocess.TimeoutExpired as error:
            def decoded(value):
                return value.decode(errors="replace") if isinstance(value, bytes) else value or ""
            stdout, stderr, code = decoded(error.stdout), decoded(error.stderr), 124
            stderr += f"\n{label} exceeded timeout={args.timeout}s\n"
        runs.append(dict(label=label, command=cmd, stdout=stdout, stderr=stderr,
                         exit_status=code))
        (args.output / f"{label}.stdout.txt").write_text(stdout, encoding="utf-8")
        (args.output / f"{label}.stderr.txt").write_text(stderr, encoding="utf-8")
        print(stdout, end="")
        print(stderr, end="", file=sys.stderr)
        if code:
            break
    comparison = None
    if args.reference_result:
        reference = Path(args.reference_result).resolve()
        old = json.loads(read_input(args, reference))
        previous = old if isinstance(old, list) else old.get("ledger", old.get("runs", []))
        last = previous[-1]
        comparison = dict(path=str(reference),
                          stdout_equal=last["stdout"] == runs[-1]["stdout"],
                          stderr_equal=last["stderr"] == runs[-1]["stderr"],
                          exit_equal=last.get("exit", last.get("exit_status")) == runs[-1]["exit_status"])
    comparable = comparison is None or all(v for k, v in comparison.items() if k != "path")
    match = re.search(r"RESULT (\d+) green (\d+) red", runs[-1]["stdout"])
    result = dict(status="pass" if runs[-1]["exit_status"] == 0 and comparable else "fail",
                  source=str(args.msm), functions=list(functions), inputs=args.inputs,
                  fixtures=args.fixtures, constants=getattr(args, "constants", None),
                  harness_sha256=sha(path), runs=runs, comparison=comparison,
                  counts={"green": int(match[1]), "red": int(match[2])} if match else None,
                  native_only_scenarios=native_only, hardware_validated=False,
                  boundaries="Real source functions; DMA/device/locks/list/attachment models. "
                             "Not kernel concurrency, hardware/TLB/retirement or full-module validation.")
    scripts = [Path(__file__).resolve(), Path(sys.argv[0]).resolve()]
    result["scripts"] = {str(p): sha(p) for p in scripts}
    (args.output / "RESULT.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("RESULT", args.output / "RESULT.json", result["status"])
    return 0 if result["status"] == "pass" else 1
