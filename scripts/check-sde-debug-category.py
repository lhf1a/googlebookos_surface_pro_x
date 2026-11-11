#!/usr/bin/env python3
"""Execute exact SDE/DP macros and paired DRM category/debug helpers on host.

The dynamic-debug descriptor/key and printk sinks are models. Actual helper,
macro, enum and debug-function bodies are extracted without rewriting.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
MACROS = ("SDE_DEBUG", "SDE_INFO", "SDE_DEBUG_DRIVER", "DP_DEBUG", "DP_INFO")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def macro(text, name, contains=None):
    lines = text.splitlines(keepends=True)
    found = []
    for i, line in enumerate(lines):
        if not re.match(r"^#define\s+" + re.escape(name) + r"(?:\(|\s)", line):
            continue
        end = i
        while lines[end].rstrip().endswith("\\"):
            end += 1
        body = "".join(lines[i:end + 1])
        if contains is None or contains in body:
            found.append(body)
    if len(found) != 1:
        raise ValueError((name, contains, len(found)))
    return found[0]


def block(text, pattern):
    matches = re.findall(pattern, text, re.M | re.S)
    if len(matches) != 1:
        raise ValueError((pattern, len(matches)))
    return matches[0]


def extract(source, kernel, output):
    msm = source if (source / "sde/sde_kms.h").exists() else source / "techpack/display/msm"
    files = [msm / "sde/sde_kms.h", msm / "dp/dp_debug.h",
             kernel / "include/drm/drm_print.h", kernel / "drivers/gpu/drm/drm_print.c",
             kernel / "include/linux/printk.h", kernel / "include/linux/dynamic_debug.h"]
    kms, dp, drm, core, printk, dynamic = [p.read_text() for p in files]
    product = "\n".join(macro(kms if name.startswith("SDE_") else dp, name) for name in MACROS)
    enum = block(drm, r"(^enum drm_debug_category \{.*?^\};)")
    raw = block(drm, r"(^static inline bool drm_debug_enabled_raw\([^;{]*\)\n\{.*?^\})")
    public = block(drm, r"(^#if defined\(CONFIG_DRM_USE_DYNAMIC_DEBUG\)\n.*?^#endif)")
    dbg = block(drm, r"(^#if !defined\(CONFIG_DRM_USE_DYNAMIC_DEBUG\)\n#define __drm_dbg\(.*?^#endif)")
    funcs = [block(core, r"(^void " + name + r"\([^;{]*\)\n\{.*?^\})") for name in ("__drm_dev_dbg", "__drm_err")]
    pieces = [enum, raw, macro(drm, "drm_debug_enabled_instrumented"), public,
              macro(dynamic, "__dynamic_func_call_cls"), macro(dynamic, "_dynamic_func_call_cls"),
              dbg, macro(drm, "_DRM_PRINTK"), macro(drm, "DRM_INFO"), macro(drm, "DRM_ERROR"),
              macro(drm, "DRM_DEBUG"), macro(printk, "no_printk"),
              "#if PRINT_MODE == 0\n" + macro(printk, "pr_debug", "no_printk") +
              "#elif PRINT_MODE == 1\n" + macro(printk, "pr_debug", "\n\tprintk(KERN_DEBUG") +
              "#else\n" + macro(printk, "pr_debug", "dynamic_pr_debug") + "#endif\n",
              "\n".join(funcs)]
    # DEBUG instrumentation expands pr_debug at call time; public macros are
    # defined first, but no function invoking them precedes the pr_debug model.
    (output / "actual-kernel.h").write_text("\n\n".join(pieces) + "\n")
    (output / "actual-product.h").write_text(product)
    return files


def check(source, kernel, output, legacy_alias=False):
    source, kernel, output = source.resolve(), kernel.resolve(), output.absolute()
    output.mkdir(parents=True, exist_ok=False)
    inputs = extract(source, kernel, output)
    harness = ROOT / "tests/sde_debug_category_test.c"
    runs = []
    env = {**os.environ, "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
           "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"}
    for dynamic in (0, 1):
        for printing in (0, 1, 2):
            name = f"d{dynamic}-p{printing}"
            cmd = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                   "-Wno-unused-parameter", "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                   f"-DPRINT_MODE={printing}", "-I", str(output), str(harness), "-o", str(output / name)]
            if dynamic:
                cmd.append("-DCONFIG_DRM_USE_DYNAMIC_DEBUG=1")
            if legacy_alias:
                cmd.append("-DTEST_LEGACY_ALIAS=1")
            for label, command in [(name + "-compile", cmd), (name + "-run", [str(output / name)])]:
                result = subprocess.run(command, capture_output=True, text=True, timeout=60, env=env)
                rec = dict(label=label, command=command, exit_status=result.returncode,
                           stdout=result.stdout, stderr=result.stderr)
                runs.append(rec)
                (output / (label + ".stdout.txt")).write_text(result.stdout)
                (output / (label + ".stderr.txt")).write_text(result.stderr)
                print(label, result.returncode, result.stdout.strip(), flush=True)
                if result.returncode and label.endswith("-compile"):
                    break
    ok = len(runs) == 12 and all(r["exit_status"] == 0 for r in runs)
    inputs += [harness, Path(__file__), output / "actual-kernel.h", output / "actual-product.h"]
    report = dict(status="host-tested-not-device-validated" if ok else "failed", source=str(source),
                  kernel_source=str(kernel), legacy_symbol_alias=legacy_alias,
                  alias_limit="Alias is an explicitly labeled semantic probe, not a real 6.18 compatibility fix.",
                  runs=runs, inputs={str(p): sha(p) for p in inputs}, all_pass=ok,
                  limits=["Exact production macros, category enum, raw/public helper and DRM debug function bodies execute.",
                          "printk sinks, dynamic-debug descriptors and branch keys are host models, not kernel static-key verification.",
                          "Non-dynamic DRM_DEBUG evaluates arguments before its function-level CORE check; dynamic disabled callsites do not.",
                          "No actual full SDE consumer, module or hardware validation is established by this host checker."])
    (output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")
    return 0 if ok else 1


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", required=True, type=Path)
    p.add_argument("--kernel-source", required=True, type=Path)
    p.add_argument("--output", required=True, type=Path)
    p.add_argument("--legacy-symbol-alias", action="store_true")
    a = p.parse_args()
    raise SystemExit(check(a.source, a.kernel_source, a.output, a.legacy_symbol_alias))
