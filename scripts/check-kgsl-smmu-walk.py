#!/usr/bin/env python3
"""Exercise actual ARM LPAE subtree unmap/free and checked callbacks on host tables."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def extract(text, name):
    bodies = re.findall(r"^static (?:inline )?(?:void|int|bool|size_t|phys_addr_t|arm_lpae_iopte)\s+" + name + r"\([^;{]*\)\n\{.*?^\}", text, re.M | re.S)
    assert len(bodies) == 1, name
    return bodies[0]


def check(source, output):
    arm = source / "drivers/iommu/io-pgtable-arm.c"
    header = source / "include/linux/io-pgtable.h"
    code, public = arm.read_text(), header.read_text()
    definitions, lines = [], iter(code.splitlines())
    for line in lines:
        if line.startswith("#define ARM_LPAE_") or line.startswith("#define iopte_type") or line.startswith("#define iopte_deref"):
            item = line
            while line.endswith("\\"):
                line = next(lines)
                item += "\n" + line
            definitions.append(item)
    struct = re.search(r"^struct arm_lpae_io_pgtable \{.*?^\};", code, re.M | re.S)
    assert struct
    definitions.append(struct[0])
    functions = [extract(public, n) for n in ("io_pgtable_tlb_flush_walk", "io_pgtable_tlb_flush_walk_checked", "io_pgtable_tlb_add_page")]
    functions += [extract(code, n) for n in ("iopte_leaf", "iopte_to_paddr", "paddr_to_iopte", "arm_lpae_max_entries", "__arm_lpae_sync_pte", "__arm_lpae_clear_pte", "__arm_lpae_free_pgtable", "__arm_lpae_unmap", "__arm_lpae_init_pte", "arm_lpae_init_pte")]
    output.mkdir(parents=True, exist_ok=False)
    (output / "smmu-walk-types.h").write_text("\n\n".join(definitions) + "\n")
    (output / "smmu-walk-functions.h").write_text("\n\n".join(functions) + "\n")
    harness = ROOT / "tests/kgsl_smmu_walk_test.c"
    cmd = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-sign-compare", "-fsanitize=address,undefined", "-fno-pie", "-no-pie", "-I", str(output), str(harness), "-o", str(output / "check")]
    build = subprocess.run(cmd, capture_output=True, text=True)
    (output / "compile.log").write_text(build.stdout + build.stderr)
    build.check_returncode()
    run = subprocess.run([str(output / "check")], capture_output=True, text=True)
    (output / "check.log").write_text(run.stdout + run.stderr)
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    report = dict(status="host-real-subtree-not-hardware-validated", exit=run.returncode, command=cmd, input_sha256={str(p):sha(p) for p in (arm, header, harness, Path(__file__))}, extracted_sha256={p.name:sha(p) for p in output.glob("*.h")}, result=run.stdout+run.stderr)
    (output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")
    run.check_returncode()
    print(run.stdout, end="")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    check(a.source.resolve(), a.output.absolute())
