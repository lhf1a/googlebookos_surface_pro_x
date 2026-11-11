#!/usr/bin/env python3
"""Run actual process-table callers with the complete native backend and modeled io-pgtable/PM."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
ROOT = Path(__file__).resolve().parents[1]


def extract(source, name):
    pattern = r"^(?:static[ \t]+)?(?:(?:unsigned[ \t]+)?(?:int|void|bool|u64|u32|uint64_t)|struct kgsl_iommu_pt[ \t]*\*)[ \t\n]+" + name + r"\([^;{]*\)\n\{.*?^\}"
    matches = list(re.finditer(pattern, source, re.M | re.S))
    assert len(matches) == 1, name
    return matches[0].group(0)


def check(source, output):
    output.mkdir(parents=True, exist_ok=False)
    inputs = []
    for name in ("kgsl_iommu_table.c", "kgsl_iommu_table.h"):
        p = source / name
        inputs.append(p)
        (output / name).write_text("\n".join(l for l in p.read_text().splitlines() if not l.startswith("#include")) + "\n")
    functions = []
    for name, names in (("kgsl_iommu.c", ("kgsl_iommu_split_tables_enabled", "_alloc_pt_metadata", "_init_per_process_pt", "_iommu_map_single_page", "_iommu_unmap", "_iommu_map_sg", "_iommu_map_guard_page", "_get_protection_flags", "kgsl_iommu_map", "kgsl_iommu_unmap", "kgsl_iommu_get_context_bank", "kgsl_iommu_destroy_pagetable", "kgsl_iommu_close")),
                        ("kgsl_mmu.c", ("_deferred_destroy", "kgsl_mmu_close")),
                        ("kgsl.c", ("kgsl_device_platform_remove",)),
                        ("adreno_iommu.c", ("adreno_iommu_set_pt_generate_cmds",))):
        p = source / name
        inputs.append(p)
        functions.extend(extract(p.read_text(), fn) for fn in names)
    (output / "process-functions.h").write_text("\n\n".join(functions) + "\n")
    harness = ROOT / "tests/kgsl_process_table_test.c"
    cmd = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-sign-compare", "-fsanitize=address,undefined", "-fno-pie", "-no-pie", "-I", str(output), str(harness), "-o", str(output / "check")]
    subprocess.run(cmd, check=True)
    run = subprocess.run([str(output / "check")], capture_output=True, text=True)
    (output / "check.log").write_text(run.stdout + run.stderr)
    inputs += [harness, ROOT / "tests/kgsl_pagetable_test.c", Path(__file__), source / "kgsl_iommu.h", source / "kgsl_mmu.h", source / "adreno_iommu.c", source / "kgsl.c"]
    result = dict(status="host-tested-not-hardware-validated" if not run.returncode else "failed", command=cmd, exit=run.returncode, result=run.stdout+run.stderr, inputs={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}, extracted={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in output.glob("*.h")})
    (output / "RESULT.json").write_text(json.dumps(result, indent=2)+"\n")
    run.check_returncode()
    print(run.stdout, end="")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    check(a.source.resolve(), a.output.absolute())
