#!/usr/bin/env python3
"""Check SDE string-copy contracts with actual driver and string bodies.

The legacy input is a real historical lib/string.c, only a host semantic
reference. It is never a product compatibility shim. The kernel-api-model build
poisons strlcpy after system includes: host libc may otherwise provide the
removed API. This is an explicit model gate, not a real kernel-header build.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def extract(text, name):
    pattern = (r"^(?:static )?(?:inline )?(?:size_t|ssize_t|bool|void|int)\s+" +
               re.escape(name) + r"\([^;{]*\)\n\{.*?^\}")
    found = re.findall(pattern, text, re.M | re.S)
    if len(found) != 1:
        raise ValueError((name, len(found)))
    return found[0]


def define(text, name):
    found = re.findall(r"^#define\s+" + re.escape(name) + r"(?:\(|\s).*?(?<!\\)$", text, re.M)
    if len(found) != 1:
        # Definitions used here can span physical lines.
        lines = text.splitlines(keepends=True)
        starts = [i for i, line in enumerate(lines) if re.match(r"^#define\s+" + re.escape(name) + r"(?:\(|\s)", line)]
        if len(starts) != 1:
            raise ValueError((name, len(starts)))
        a = b = starts[0]
        while lines[b].rstrip().endswith("\\"):
            b += 1
        return "".join(lines[a:b + 1])
    return found[0] + "\n"


def check(source, kernel, legacy, output):
    source, kernel, legacy, output = source.resolve(), kernel.resolve(), legacy.resolve(), output.absolute()
    msm = source if (source / "sde_dbg_evtlog.c").exists() else source / "techpack/display/msm"
    evtlog = msm / "sde_dbg_evtlog.c"
    parser = msm / "dsi/dsi_parser.c"
    catalog = msm / "dp/dp_catalog.c"
    string = kernel / "lib/string.c"
    string_h = kernel / "include/linux/string.h"
    words = kernel / "include/asm-generic/word-at-a-time.h"
    sizes = kernel / "include/linux/sizes.h"
    output.mkdir(parents=True, exist_ok=False)
    (output / "actual-legacy-string.h").write_text(extract(legacy.read_text(), "strlcpy") + "\n")
    word_text = "\n".join(line for line in words.read_text().splitlines()
                          if not line.startswith("#include "))
    (output / "actual-word-helpers.h").write_text(word_text + "\n")
    kernel_defs = (define(string_h.read_text(), "__strscpy1") +
                   "#define strscpy(dst, src, size) __strscpy1(dst, src, size)\n")
    # Only the three-argument dispatcher spelling is modeled; its actual
    # __strscpy1 target and entire sized_strscpy body execute unchanged.
    (output / "actual-kernel-string.h").write_text(kernel_defs + extract(string.read_text(), "sized_strscpy") + "\n")
    defs = define(evtlog.read_text(), "SDE_EVTLOG_FILTER_STRSIZE") + define(sizes.read_text(), "SZ_4")
    (output / "actual-definitions.h").write_text(defs)
    functions = [(evtlog, "sde_evtlog_get_filter"), (parser, "dsi_parser_parse_prop"),
                 (catalog, "dp_catalog_set_exe_mode")]
    (output / "actual-driver-functions.h").write_text("\n\n".join(extract(path.read_text(), name) for path, name in functions) + "\n")
    runs = []
    harness = ROOT / "tests/sde_strlcpy_test.c"
    env = {**os.environ, "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
           "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"}
    for label, flags in [("kernel-api-model", ["-DNATIVE_API=1", "-DCONFIG_KMSAN=1"]),
                         ("scalar", ["-DCONFIG_KMSAN=1"]),
                         ("word", ["-DCONFIG_KMSAN=0"])]:
        commands = [(label + "-compile", ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-parameter", "-Wno-unused-function", "-fsanitize=address,undefined",
                    "-fno-pie", "-no-pie", "-I", str(output), *flags, str(harness), "-o", str(output / label)]),
                    (label + "-run", [str(output / label)])]
        for name, command in commands:
            result = subprocess.run(command, capture_output=True, text=True, timeout=60, env=env)
            rec = dict(label=name, command=command, exit_status=result.returncode,
                       stdout=result.stdout, stderr=result.stderr)
            runs.append(rec)
            (output / (name + ".stdout.txt")).write_text(result.stdout)
            (output / (name + ".stderr.txt")).write_text(result.stderr)
            print(name, result.returncode, result.stdout.strip(), flush=True)
            if name.endswith("compile") and result.returncode:
                break
    paths = [evtlog, parser, catalog, string, string_h, words, sizes, legacy, harness, Path(__file__)]
    remaining = []
    copy_files = []
    for path in sorted(msm.rglob("*")):
        if path.suffix not in (".c", ".h"):
            continue
        text = path.read_text()
        if re.search(r"\bstr(?:l|s)cpy\s*\(", text):
            copy_files.append(path)
        for match in re.finditer(r"\bstrlcpy\s*\(", text):
            remaining.append(dict(path=str(path), line=text.count("\n", 0, match.start()) + 1))
    paths += copy_files + [output / name for name in ("actual-legacy-string.h", "actual-word-helpers.h",
                          "actual-kernel-string.h", "actual-definitions.h", "actual-driver-functions.h")]
    ok = len(runs) == 6 and all(r["exit_status"] == 0 for r in runs) and not remaining
    report = dict(status="host-tested-not-device-validated" if ok else "failed", runs=runs, all_pass=ok,
                  source=str(source), kernel_source=str(kernel), legacy_reference=str(legacy),
                  inputs={str(p): sha(p) for p in paths}, functions=[name for _, name in functions],
                  remaining_strlcpy_sites=remaining,
                  api_gate=dict(kind="host-compiler-poison-model", actual_kernel_build=False,
                                detail="System libc remains included; fixture explicitly poisons strlcpy after those headers."),
                  api_model_compile_pass=next(r["exit_status"] == 0 for r in runs if r["label"] == "kernel-api-model-compile"),
                  source_api_scan_pass=not remaining,
                  limits=["Kernel-api-model uses an explicit compiler poison after libc includes; it is not a real Linux kernel compilation.",
                          "Historical native-api results without poison could link host glibc strlcpy; wrapper failure from remaining-site scanning was not compilation failure.",
                          "Semantic modes use a real historical strlcpy implementation solely as a host reference.",
                          "Actual sized_strscpy and generic word helpers execute; word-load, allocation, parser leaf and lock APIs are modeled.",
                          "Only three complete driver functions execute; every remaining call site must also be audited statically.",
                          "Word-path source storage is padded; exact-sized sources are checked in the actual scalar branch.",
                          "Bytes after the first NUL inside the bound are not promised identical by strscpy; outer canaries and C-string values are checked.",
                          "No complete SDE consumer/module, kernel concurrency, hardware or deployment validation."])
    (output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")
    return 0 if ok else 1


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", required=True, type=Path)
    p.add_argument("--kernel-source", required=True, type=Path)
    p.add_argument("--legacy-string-source", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    a = p.parse_args()
    raise SystemExit(check(a.source, a.kernel_source, a.legacy_string_source, a.output))
