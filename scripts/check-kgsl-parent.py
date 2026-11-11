#!/usr/bin/env python3
"""Exercise actual native parent/probe/global ownership with modeled kernel and SMMU services."""
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
    groups = (("kgsl_iommu.c", ("global_get_offset", "kgsl_iommu_map_global", "kgsl_iommu_unmap_globals", "kgsl_iommu_release_parent", "_init_global_pt", "kgsl_iommu_close", "kgsl_iommu_probe")), ("kgsl_mmu.c", ("kgsl_mmu_createpagetableobject",)), ("kgsl_sharedmem.c", ("kgsl_fixed_global_free", "kgsl_map_global_allocation", "kgsl_free_globals")))
    for name, names in groups:
        p = source / name
        inputs.append(p)
        for fn in names:
            pattern = r"^(?:static )?(?:int|void|u64|struct kgsl_pagetable[ \t]*\*)[ \t\n]+" + fn + r"\([^;{]*\)\n\{.*?^\}"
            found = re.findall(pattern, p.read_text(), re.M | re.S)
            assert len(found) == 1, fn
            functions.append(found[0])
    (output / "parent-functions.h").write_text("\n\n".join(functions) + "\n")
    harness = ROOT / "tests/kgsl_parent_test.c"
    command = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-sign-compare", "-fsanitize=address,undefined", "-fno-pie", "-no-pie", "-I", str(output), str(harness), "-o", str(output / "check")]
    build = subprocess.run(command, capture_output=True, text=True)
    (output / "compile.log").write_text(build.stdout + build.stderr)
    build.check_returncode()
    run = subprocess.run([str(output / "check")], capture_output=True, text=True)
    (output / "check.log").write_text(run.stdout + run.stderr)
    inputs += [harness, Path(__file__), source / "kgsl_iommu.h", source / "kgsl_mmu.h"]
    result = dict(status="host-tested-not-hardware-validated" if not run.returncode else "failed", exit=run.returncode, result=run.stdout+run.stderr, command=command, inputs={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}, extracted_sha256=hashlib.sha256((output / "parent-functions.h").read_bytes()).hexdigest())
    (output / "RESULT.json").write_text(json.dumps(result, indent=2)+"\n")
    run.check_returncode()
    print(run.stdout,end="")
if __name__ == "__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source",type=Path,required=True)
    p.add_argument("--output",type=Path,required=True)
    a=p.parse_args()
    check(a.source.resolve(),a.output.absolute())
