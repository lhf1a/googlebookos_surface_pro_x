#!/usr/bin/env python3
"""Check the component/devres retention contract, not hardware-safe teardown.

Default: return 1 when the actual callback/core chain violates retention.
--expect-gap: return 0 only when the known two-case gap is reproduced, including
the strict executable's exit 1. This is never a lifecycle acceptance result.
No kernel source is downloaded or modified; all dependencies are explicit.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
EXTRACTOR = ROOT / "scripts/check-kgsl-dmabuf.py"
spec = importlib.util.spec_from_file_location("kgsl_extract", EXTRACTOR)
extractor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(extractor)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check(source, kernel_source, output, expect_gap=False):
    source, kernel_source, output = source.resolve(), kernel_source.resolve(), output.absolute()
    items = [
        (kernel_source / "drivers/base/component.c", (
            "component_unbind", "component_unbind_all", "take_down_aggregate_device")),
        (source / "adreno_a6xx_gmu.c", ("a6xx_gmu_unbind",)),
        (source / "adreno_a6xx_hwsched.c", ("a6xx_hwsched_unbind",)),
        (source / "adreno.c", ("adreno_unbind_gmu",)),
    ]
    functions = {name: extractor.extract(path.read_text(), name)
                 for path, names in items for name in names}
    output.mkdir(parents=True, exist_ok=False)
    order = ("a6xx_gmu_unbind", "a6xx_hwsched_unbind", "component_unbind",
             "component_unbind_all", "adreno_unbind_gmu", "take_down_aggregate_device")
    extracted = output / "component-teardown-functions.h"
    extracted.write_text("\n\n".join(functions[name] for name in order) + "\n")
    harness = ROOT / "tests/kgsl_component_teardown_test.c"
    command = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
               "-Wno-unused-parameter", "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
               "-I", str(output), str(harness), "-o", str(output / "check")]
    runs = []

    def run(label, command):
        result = subprocess.run(command, capture_output=True, text=True,
                                env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})
        (output / (label + ".stdout")).write_text(result.stdout)
        (output / (label + ".stderr")).write_text(result.stderr)
        runs.append(dict(label=label, command=command, exit=result.returncode,
                         stdout=result.stdout, stderr=result.stderr))
        return result

    compiled = run("compile", command)
    observed = strict = None
    if compiled.returncode == 0:
        observed = run("observe", [str(output / "check"), "--observe"])
        strict = run("strict", [str(output / "check")])
    gap = bool(observed is not None and strict is not None and
               observed.returncode == 0 and strict.returncode == 1 and
               "observed_gap_cases=2;" in observed.stdout and
               observed.stdout == strict.stdout and not observed.stderr and
               strict.stderr == "FAIL: cleanup error did not retain component/master devres ownership\n")
    retained = bool(observed is not None and strict is not None and
                    observed.returncode == 0 and strict.returncode == 0 and
                    "observed_gap_cases=0;" in observed.stdout and
                    observed.stdout == strict.stdout and not observed.stderr and not strict.stderr)
    status = ("known-retention-gap-reproduced" if gap else
              "modeled-retention-contract-satisfied" if retained else "execution-or-contract-error")
    exit_code = (0 if gap else 1) if expect_gap else (0 if retained else 1)
    inputs = [path for path, _ in items] + [harness, Path(__file__), EXTRACTOR]
    report = dict(
        status=status, expect_gap=expect_gap, exit=exit_code,
        lifecycle_accepted=False, hardware_validated=False,
        inputs={str(path): sha(path) for path in inputs},
        functions={name: dict(source=str(path), sha256=hashlib.sha256(
            functions[name].encode()).hexdigest()) for path, names in items for name in names},
        extracted_sha256=sha(extracted), runs=runs,
        limitations=[
            "Actual six callback/component functions; structures and kernel leaves are models.",
            "a6xx_gmu_remove injects errno; devres release counts calls, not actual deallocation.",
            "Master wrapper invokes adreno_unbind_gmu, not the entire adreno_unbind function.",
            "No full driver-core remove/probe unwind, locking, concurrency, DMA or GPU validation.",
            "A zero --expect-gap exit confirms the known defect, never safe teardown.",
        ])
    (output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")
    (output / "VERIFICATION.txt").write_text("\n\n".join(
        "COMMAND: " + json.dumps(r["command"]) + "\nEXIT: " + str(r["exit"]) +
        "\nSTDOUT:\n" + r["stdout"] + "STDERR:\n" + r["stderr"] for r in runs) + "\n")
    print(status, "exit=" + str(exit_code), "lifecycle_accepted=false", output)
    return exit_code


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True,
                        help="Prepared KGSL drivers/gpu/msm directory")
    parser.add_argument("--kernel-source", type=Path, required=True,
                        help="Matching Linux source root containing drivers/base/component.c")
    parser.add_argument("--output", type=Path, required=True, help="Fresh evidence directory")
    parser.add_argument("--expect-gap", action="store_true",
                        help="Expect the known retention defect; not a lifecycle acceptance mode")
    args = parser.parse_args()
    raise SystemExit(check(args.source, args.kernel_source, args.output, args.expect_gap))
