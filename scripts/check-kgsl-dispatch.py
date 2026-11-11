#!/usr/bin/env python3
"""Check actual dispatcher reset failure retention and cancellation ordering; hardware operations mocked."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("kgsl_check", ROOT / "scripts/check-kgsl-dmabuf.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def check(source, output):
    functions, hashes = [], {}
    for name, names in (
        ("adreno.h", ("adreno_clear_gpu_fault",)),
        ("adreno.c", ("adreno_stop",)),
        ("adreno_dispatch.c", ("sendcmd", "adreno_gpu_stopped", "_adreno_dispatcher_issuecmds", "remove_invalidated_cmdobjs", "recover_dispatch_q", "dispatcher_fault_failed", "adreno_dispatch_retire_drawqueue", "dispatcher_do_fault", "adreno_dispatcher_work", "adreno_dispatcher_fault_timer")),
        ("adreno_ringbuffer.c", ("adreno_ringbuffer_wait_callback", "adreno_ringbuffer_waittimestamp")),
    ):
        path = source / name
        functions.extend(module.extract(path.read_text(), f) for f in names)
        hashes[name] = hashlib.sha256(path.read_bytes()).hexdigest()
    hashes["adreno_dispatch.h"] = hashlib.sha256((source / "adreno_dispatch.h").read_bytes()).hexdigest()
    output.mkdir(parents=True, exist_ok=False)
    extracted = output / "kgsl-dispatch-functions.h"
    extracted.write_text("\n\n".join(functions) + "\n")
    harness = ROOT / "tests/kgsl_dispatch_test.c"
    command = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
               "-Wno-unused-parameter", "-Wno-sign-compare", "-fsanitize=address,undefined",
               "-fno-pie", "-no-pie", "-I", str(output), str(harness), "-o", str(output / "check")]
    subprocess.run(command, check=True)
    result = subprocess.run([str(output / "check")], capture_output=True, text=True)
    (output / "check.log").write_text(result.stdout + result.stderr)
    report = dict(status="host-tested-not-hardware-validated" if result.returncode == 0 else "failed",
                  source_sha256=hashes, command=command, exit=result.returncode,
                  harness_sha256=hashlib.sha256(harness.read_bytes()).hexdigest(),
                  extracted_sha256=hashlib.sha256(extracted.read_bytes()).hexdigest(),
                  result=result.stdout + result.stderr,
                  checker_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  limits="Actual dispatcher fault/replay/send/stop/wait functions; reset/ringbuffer/MMIO/events/fault-policy/locks/timers and object-reference leaves modeled. 8-slot model exercises wrap and actual target uses 128 slots. Not GPU retirement, kernel concurrency, retained-owner recovery or safe unbind proof.")
    (output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")
    result.check_returncode()
    print(result.stdout, end="")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="Prepared drivers/gpu/msm directory")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    check(args.source.resolve(), args.output.absolute())
