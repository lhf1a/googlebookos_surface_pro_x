#!/usr/bin/env python3
"""Check actual A6xx restore-record reset and the checked GMU restart caller."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
ROOT = Path(__file__).resolve().parents[1]
def extract(text, name):
    matches = re.findall(r"^(?:static )?(?:inline )?(?:int|void|bool)\s+" + re.escape(name) + r"\([^;{]*\)\n\{.*?^\}", text, re.M | re.S)
    assert len(matches) == 1, name
    return matches[0]
def check(source, output):
    output.mkdir(parents=True, exist_ok=False)
    inputs, records, functions = [], [], []
    for file, names in (("adreno_a6xx.h", ("a6xx_cp_preemption_record", "a6xx_cp_smmu_info")), ("adreno_ringbuffer.h", ("adreno_ringbuffer_pagetable_info",))):
        path = source / file
        inputs.append(path)
        for name in names:
            records.append(re.search(r"^struct " + name + r" \{.*?^\};", path.read_text(), re.M | re.S)[0])
    (output / "hard-reset-records.h").write_text("\n\n".join(records) + "\n")
    for file, names in (("kgsl_sharedmem.c", ("kgsl_sharedmem_writel", "kgsl_sharedmem_writeq")), ("adreno.h", ("adreno_ringbuffer_set_pagetable",)), ("adreno_a6xx_preempt.c", ("a6xx_reset_mem_valid", "a6xx_preemption_reset_records")), ("adreno_a6xx_gmu.c", ("a6xx_gmu_retire_contexts", "a6xx_gmu_restart"))):
        path = source / file
        inputs.append(path)
        functions.extend(extract(path.read_text(), name) for name in names)
    header = output / "hard-reset-functions.h"
    header.write_text("\n\n".join(functions) + "\n")
    harness = ROOT / "tests/kgsl_hard_reset_test.c"
    cmd = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-sign-compare", "-fsanitize=address,undefined", "-fno-pie", "-no-pie", "-I", str(output), str(harness), "-o", str(output / "check")]
    build = subprocess.run(cmd, capture_output=True, text=True)
    (output / "compile.log").write_text(build.stdout + build.stderr)
    build.check_returncode()
    run = subprocess.run([str(output / "check")], capture_output=True, text=True)
    (output / "check.log").write_text(run.stdout + run.stderr)
    inputs += [header, output / "hard-reset-records.h", harness, Path(__file__)]
    report = dict(exit=run.returncode, output=run.stdout, command=cmd, sha256={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}, limits="Actual record layouts, memory writers, root-record setter, reset records and GMU restart. Shutdown hardware/provider/boot/ref-release leaves modeled. No actual GPU, cache-coherency, bus drainage, preemption firmware or safe-unbind validation.")
    (output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")
    run.check_returncode()
    print(run.stdout, end="")
if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    a = p.parse_args()
    check(a.source.resolve(), a.output.absolute())
