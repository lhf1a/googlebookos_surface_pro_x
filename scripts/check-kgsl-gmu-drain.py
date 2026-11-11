#!/usr/bin/env python3
"""Exercise actual ARM SMMU checked drain and Qualcomm poll with modeled MMIO/PM."""
import argparse
import hashlib
import importlib.util
import json
import re
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("extractor", ROOT / "scripts/check-kgsl-dmabuf.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def check(source, output):
    functions, hashes = [], {}
    for name, names in (
        ("arm-smmu-qcom.c", ("qcom_smmu_tlb_sync_checked",)),
        ("arm-smmu.c", ("arm_smmu_flush_tlb_checked", "arm_smmu_enable_checked_unmap",
                        "arm_smmu_tlb_inv_walk_s1_checked", "arm_smmu_map_pages", "arm_smmu_unmap_pages")),
    ):
        path = source / "drivers/iommu/arm/arm-smmu" / name
        for f in names:
            if f == "arm_smmu_unmap_pages":
                bodies = re.findall(r"^static size_t " + f + r"\([^;{]*\)\n\{.*?^\}", path.read_text(), re.M | re.S)
                assert len(bodies) == 1
                functions.append(bodies[0])
            else:
                functions.append(module.extract(path.read_text(), f))
        hashes[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
    core = (source / "drivers/iommu/arm/arm-smmu/arm-smmu.c").read_text()
    wiring = re.findall(r"^static const struct iommu_flush_ops arm_smmu_s1_tlb_ops = \{.*?^\};", core, re.M | re.S)
    assert len(wiring) == 1
    functions.append(wiring[0])
    output.mkdir(parents=True, exist_ok=False)
    extracted = output / "gmu-drain-functions.h"
    extracted.write_text("\n\n".join(functions) + "\n")
    harness = ROOT / "tests/kgsl_gmu_drain_test.c"
    command = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
               "-Wno-unused-parameter", "-fsanitize=address,undefined",
               "-fno-pie", "-no-pie", "-I", str(output), str(harness), "-o", str(output / "check")]
    subprocess.run(command, check=True)
    run = subprocess.run([str(output / "check")], capture_output=True, text=True)
    (output / "check.log").write_text(run.stdout + run.stderr)
    report = dict(status="host-functions-not-hardware-validated", exit=run.returncode,
                  command=command, input_sha256=hashes,
                  harness_sha256=hashlib.sha256(harness.read_bytes()).hexdigest(),
                  extracted_sha256=hashlib.sha256(extracted.read_bytes()).hexdigest(),
                  result=run.stdout + run.stderr)
    (output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")
    run.check_returncode()
    print(run.stdout, end="")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    check(args.source.resolve(), args.output.absolute())
