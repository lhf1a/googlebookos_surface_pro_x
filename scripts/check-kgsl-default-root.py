#!/usr/bin/env python3
"""Check native default-root restoration and the actual soft-reset caller."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
ROOT = Path(__file__).resolve().parents[1]

def check(source, output):
    output.mkdir(parents=True, exist_ok=False)
    functions, inputs = [], []
    for name, fn, rename in (
        ("kgsl_iommu.c", "kgsl_iommu_set_pt", None),
        ("kgsl_mmu.h", "kgsl_mmu_set_pt", None),
        ("adreno.c", "_soft_reset", None),
        ("adreno.c", "adreno_set_active_ctxs_null", None),
        ("adreno.c", "adreno_soft_reset", None),
        ("adreno.c", "_adreno_start", None),
        ("adreno_a6xx_gmu.c", "a6xx_gpu_boot", "gmu_gpu_boot"),
        ("adreno_a6xx_rgmu.c", "a6xx_gpu_boot", "rgmu_gpu_boot"),
    ):
        p = source / name
        inputs.append(p)
        pattern = r"^(?:static )?(?:inline )?(?:int|void) " + fn + r"\([^;{]*\)\n\{.*?^\}"
        matches = re.findall(pattern, p.read_text(), re.M | re.S)
        assert len(matches) == 1, fn
        functions.append(matches[0].replace(fn + "(", rename + "(", 1) if rename else matches[0])
    (output / "default-root-functions.h").write_text("\n\n".join(functions) + "\n")
    harness = ROOT / "tests/kgsl_default_root_test.c"
    command = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-fsanitize=address,undefined", "-fno-pie", "-no-pie", "-I", str(output), str(harness), "-o", str(output / "check")]
    build = subprocess.run(command, capture_output=True, text=True)
    (output / "compile.log").write_text(build.stdout + build.stderr)
    build.check_returncode()
    run = subprocess.run([str(output / "check")], capture_output=True, text=True)
    (output / "check.log").write_text(run.stdout + run.stderr)
    inputs += [harness, Path(__file__)]
    report = dict(status="host-tested-not-hardware-validated" if not run.returncode else "failed", command=command, exit=run.returncode, result=run.stdout + run.stderr, inputs={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}, extracted_sha256=hashlib.sha256((output / "default-root-functions.h").read_bytes()).hexdigest())
    (output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")
    run.check_returncode()
    print(run.stdout, end="")

if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    check(a.source.resolve(), a.output.absolute())
