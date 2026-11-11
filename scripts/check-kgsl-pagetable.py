#!/usr/bin/env python3
"""Fault-inject the complete native KGSL io-pgtable backend; no hardware proof."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def check(source, output):
    output.mkdir(parents=True, exist_ok=False)
    inputs = []
    for name in ("kgsl_iommu_table.h", "kgsl_iommu_table.c"):
        path = source / name
        inputs.append(path)
        # Compile every production function, replacing only the kernel dependencies.
        (output / name).write_text("\n".join(line for line in path.read_text().splitlines()
                                             if not line.startswith("#include")) + "\n")
    harness = ROOT / "tests/kgsl_pagetable_test.c"
    command = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
               "-fsanitize=address,undefined", "-fno-pie", "-no-pie", "-I", str(output),
               str(harness), "-o", str(output / "check")]
    subprocess.run(command, check=True)
    result = subprocess.run([str(output / "check")], capture_output=True, text=True)
    inputs += [harness, Path(__file__).resolve()]
    report = dict(status="host-tested-not-hardware-validated" if result.returncode == 0 else "failed",
                  inputs={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},
                  command=command, exit=result.returncode, result=result.stdout + result.stderr)
    (output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")
    (output / "check.log").write_text(result.stdout + result.stderr)
    result.check_returncode()
    print(result.stdout, end="")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="Prepared drivers/gpu/msm directory")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    check(args.source.resolve(), args.output.absolute())
