#!/usr/bin/env python3
"""Fault-inject actual Xiaomi KGSL bus setters; does not execute GPU/ICC hardware."""
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
    for name, function in (("adreno.c", "adreno_interconnect_bus_set"),
                           ("adreno_a6xx_gmu.c", "a6xx_gmu_bus_set"),
                           ("adreno_a6xx_hwsched.c", "a6xx_hwsched_bus_set")):
        path = source / name
        functions.append(module.extract(path.read_text(), function))
        hashes[name] = hashlib.sha256(path.read_bytes()).hexdigest()
    bus = source / "kgsl_bus.c"
    functions.insert(0, module.extract(bus.read_text(), "kgsl_bus_set_icc"))
    hashes["kgsl_bus.c"] = hashlib.sha256(bus.read_bytes()).hexdigest()
    output.mkdir(parents=True, exist_ok=False)
    (output / "kgsl-icc-functions.h").write_text("\n\n".join(functions) + "\n")
    harness = ROOT / "tests/kgsl_icc_test.c"
    command = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
               "-fsanitize=address,undefined", "-fno-pie", "-no-pie", "-I", str(output),
               str(harness), "-o", str(output / "check")]
    subprocess.run(command, check=True)
    result = subprocess.run([str(output / "check")], capture_output=True, text=True)
    (output / "check.log").write_text(result.stdout + result.stderr)
    report = dict(status="host-tested-not-hardware-validated" if result.returncode == 0 else "failed",
                  source_sha256=hashes, command=command, exit=result.returncode,
                  harness_sha256=hashlib.sha256(harness.read_bytes()).hexdigest(),
                  result=result.stdout + result.stderr)
    (output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")
    result.check_returncode()
    print(result.stdout, end="")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="Prepared drivers/gpu/msm directory")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    check(args.source.resolve(), args.output.absolute())
