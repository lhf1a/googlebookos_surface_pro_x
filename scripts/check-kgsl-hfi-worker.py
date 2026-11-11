#!/usr/bin/env python3
"""Execute actual HFI probe/worker functions with pthread wait/stop and mocked IRQ/slab.

This verifies the probe-only cleanup contract, not live hardware teardown.
Baseline sources remain testable and must fail the blocked worker stop case.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
EXTRACTOR = ROOT / "scripts/check-kgsl-dmabuf.py"
spec = importlib.util.spec_from_file_location("kgsl_extract", EXTRACTOR)
extractor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(extractor)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check(source, output):
    source, output = source.resolve(), output.absolute()
    source_files = [source / name for name in (
        "adreno_a6xx_hwsched_hfi.c", "adreno_a6xx_hwsched_hfi.h", "adreno_a6xx_gmu.c")]
    body = source_files[0].read_text()
    irq = extractor.extract(source_files[2].read_text(), "a6xx_gmu_request_irq")
    # The only producer is the HFI IRQ. No successful IRQ request may enable it
    # before the probe returns; failure must propagate rather than claim success.
    if "IRQF_TRIGGER_HIGH | IRQF_NO_AUTOEN" not in irq or "return ret ? ret : irq;" not in irq:
        raise ValueError("The tested probe requires the real NO_AUTOEN IRQ helper contract")
    names = ["add_f2h_packet", "adreno_a6xx_add_log_block", "hfi_f2h_main"]
    if "static void a6xx_hwsched_f2h_probe_cleanup(" in body:
        names.append("a6xx_hwsched_f2h_probe_cleanup")
    names.append("a6xx_hwsched_hfi_probe")
    functions = {name: extractor.extract(body, name) for name in names}
    types = re.search(r"struct f2h_packet \{.*?\n};", body, re.S).group() + "\n"
    if "static struct kmem_cache *f2h_cache;" in body:
        types += "static struct kmem_cache *f2h_cache;\n"
    output.mkdir(parents=True, exist_ok=False)
    extracted = output / "actual-hfi-functions.h"
    extracted.write_text(types + "\n\n".join(functions.values()) + "\n")
    harness = ROOT / "tests/kgsl_hfi_worker_test.c"
    command = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
               "-Wno-unused-parameter", "-Wno-unused-function",  # baseline omits some mock helpers
               "-fsanitize=address,undefined", "-fno-pie", "-no-pie", "-pthread",
               "-I", str(output), str(harness), "-o", str(output / "check")]
    runs = []

    def run(label, command):
        try:
            result = subprocess.run(command, capture_output=True, text=True, timeout=30,
                                    env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})
            stdout, stderr, status = result.stdout, result.stderr, result.returncode
        except subprocess.TimeoutExpired as exc:
            stdout, stderr, status = exc.stdout or "", exc.stderr or "", 124
            stdout = stdout.decode(errors="replace") if isinstance(stdout, bytes) else stdout
            stderr = stderr.decode(errors="replace") if isinstance(stderr, bytes) else stderr
        (output / (label + ".stdout")).write_text(stdout)
        (output / (label + ".stderr")).write_text(stderr)
        runs.append(dict(label=label, command=command, exit=status, stdout=stdout, stderr=stderr))
        return status

    compiled = run("compile", command)
    if compiled == 0:
        run("check", [str(output / "check")])
    status = runs[-1]["exit"]
    report = dict(
        status="host-functions-not-hardware-validated" if status == 0 else "failed",
        exit=status, hardware_validated=False, live_teardown_validated=False,
        input_sha256={str(path): sha(path) for path in source_files + [harness, Path(__file__), EXTRACTOR]},
        functions={name: hashlib.sha256(text.encode()).hexdigest() for name, text in functions.items()},
        extracted_sha256=sha(extracted), irq_helper_source_checked_not_executed=True,
        runs=runs,
        limitations=[
            "Actual five candidate functions and packet type, with modeled kernel dependencies.",
            "Real pthread blocking/join is not Linux scheduler, weak-memory or hardware IRQ validation.",
            "IRQ remains unenabled in this probe-only failure model; successful runtime teardown is not wired.",
            "Test success-instance cleanup is a harness operation after modeled producers cease.",
        ])
    (output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")
    (output / "VERIFICATION.txt").write_text("\n\n".join(
        "COMMAND: " + json.dumps(r["command"]) + "\nEXIT: " + str(r["exit"]) +
        "\nSTDOUT:\n" + r["stdout"] + "STDERR:\n" + r["stderr"] for r in runs) + "\n")
    print(runs[-1]["stdout"], end="")
    if status:
        print("HFI probe/worker check failed:", status, output)
    return status if status >= 0 else 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True,
                        help="Prepared KGSL drivers/gpu/msm directory")
    parser.add_argument("--output", type=Path, required=True, help="Fresh evidence directory")
    args = parser.parse_args()
    raise SystemExit(check(args.source, args.output))
