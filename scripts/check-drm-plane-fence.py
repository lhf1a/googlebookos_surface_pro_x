#!/usr/bin/env python3
"""Execute the actual DRM prepare_fb helper with modeled fence/resv/alloc leaves.

ASan/UBSan host ownership regression only; no kernel build, KOUT or device access.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
CASES = ("no-fb-explicit", "chain-explicit", "chain-implicit", "all")


def check(kernel_source, output, cases):
    source = kernel_source / "drivers/gpu/drm/drm_gem_atomic_helper.c"
    harness = ROOT / "tests/drm_plane_fence_test.c"
    text = source.read_text()
    match = re.search(r"^int drm_gem_plane_helper_prepare_fb\(.*?^\}", text, re.S | re.M)
    if not match:
        raise ValueError("Actual drm_gem_plane_helper_prepare_fb body not found")
    output.mkdir(parents=True, exist_ok=False)
    extracted = output / "actual-function.h"
    extracted.write_text(match.group(0) + "\n")
    env = {**os.environ, "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
           "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"}
    compile_command = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                       "-Wno-unused-parameter", "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                       "-I", str(output), str(harness), "-o", str(output / "check")]
    commands = [("compile", compile_command)] + [(case, [str(output / "check"), case]) for case in cases]
    runs = []
    for label, command in commands:
        result = subprocess.run(command, capture_output=True, text=True, timeout=60, env=env)
        runs.append(dict(label=label, command=command, exit_status=result.returncode,
                         stdout=result.stdout, stderr=result.stderr))
        (output / (label + ".stdout.txt")).write_text(result.stdout)
        (output / (label + ".stderr.txt")).write_text(result.stderr)
        if label == "compile" and result.returncode:
            break
    success = len(runs) == len(commands) and all(run["exit_status"] == 0 for run in runs)
    report = dict(status="host-ownership-verified-not-kernel-built" if success else "failed",
                  function="drm_gem_plane_helper_prepare_fb", cases=cases, runs=runs,
                  inputs={str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                          for path in (source, harness, Path(__file__), extracted)},
                  environment={key: env[key] for key in ("ASAN_OPTIONS", "UBSAN_OPTIONS")},
                  hardware_validated=False, kernel_built=False,
                  limits=["Only the actual helper body is extracted unchanged; API leaves are modeled.",
                          "The WRITE model result represents the API's aggregate singleton, not only WRITE-class fences.",
                          "No kernel concurrency, actual scanout or complete SDE acceptance is established."])
    (output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")
    for run in runs:
        print(run["stdout"], end="")
    print("RESULT", output / "RESULT.json", report["status"])
    return 0 if success else 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kernel-source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--case", action="append", choices=CASES,
                        help="Repeat for selected inputs; defaults to the three leak cases plus all 41 scenarios")
    args = parser.parse_args()
    raise SystemExit(check(args.kernel_source.resolve(), args.output.absolute(), args.case or list(CASES)))
