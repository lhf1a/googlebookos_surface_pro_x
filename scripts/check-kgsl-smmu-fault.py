#!/usr/bin/env python3
"""Exercise actual SMMU fault ownership and KGSL policy/recovery helpers with mocked PM/MMIO."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def check(provider, source, kernel, output):
    spec = importlib.util.spec_from_file_location("extract", ROOT / "scripts/check-kgsl-dmabuf.py")
    ex = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ex)
    output.mkdir(parents=True, exist_ok=False)
    p = provider / "drivers/iommu/arm/arm-smmu/arm-smmu-qcom.c"
    k = kernel / "drivers/iommu/arm/arm-smmu/arm-smmu.h"
    text = k.read_text()
    defines = []
    for match in re.finditer(r"^#define (?:[^\n]*\\\n)*[^\n]*", text, re.M):
        name = match.group(0).split()[1]
        if name.startswith(("ARM_SMMU_CB_FSR", "ARM_SMMU_CB_FSYNR0", "ARM_SMMU_SCTLR_")) or name in ("ARM_SMMU_CB_SCTLR", "ARM_SMMU_CB_RESUME", "ARM_SMMU_RESUME_TERMINATE"):
            defines.append(match.group(0))
    defines.append(re.search(r"struct arm_smmu_context_fault_info \{.*?^\};", text, re.M | re.S).group(0))
    (output / "fault-constants.h").write_text("\n".join(defines) + "\n")
    functions = [ex.extract(p.read_text(), name) for name in ("qcom_adreno_smmu_write_sctlr", "qcom_adreno_smmu_set_fault_policy", "qcom_adreno_smmu_terminate_fault", "qcom_adreno_smmu_rearm_fault")]
    functions.append(re.search(r"^static irqreturn_t qcom_adreno_smmu_context_fault\([^;{]*\)\n\{.*?^\}", p.read_text(), re.M | re.S).group(0))
    (output / "fault-provider.h").write_text("\n\n".join(functions) + "\n")
    gpu = source / "kgsl_iommu.c"
    kgsl_header = (source / "kgsl_iommu.h").read_text()
    for bit in ("TF", "PF", "EF"):
        kgsl_value = re.search(r"^#define KGSL_IOMMU_FSR_" + bit + r"\s+([^\n]+)", kgsl_header, re.M).group(1)
        core_value = re.search(r"^#define ARM_SMMU_CB_FSR_" + bit + r"\s+([^\n]+)", text, re.M).group(1)
        assert kgsl_value.strip() == core_value.strip()
    assert re.search(r"^#define KGSL_IOMMU_FSR_SS_SHIFT\s+30$", kgsl_header, re.M)
    assert re.search(r"^#define ARM_SMMU_CB_FSR_SS\s+BIT\(30\)$", text, re.M)
    functions = [ex.extract(gpu.read_text(), name) for name in ("_setup_user_context", "kgsl_iommu_clear_fsr", "kgsl_iommu_pagefault_resume", "kgsl_iommu_set_pf_policy")]
    (output / "fault-consumer.h").write_text("\n\n".join(functions) + "\n")
    (output / "fault-handler.h").write_text(ex.extract(gpu.read_text(), "kgsl_iommu_fault_handler") + "\n")
    harness = ROOT / "tests/kgsl_smmu_fault_test.c"
    cmd = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-sign-compare", "-fsanitize=address,undefined", "-fno-pie", "-no-pie", "-I", str(output), str(harness), "-o", str(output / "check")]
    subprocess.run(cmd, check=True)
    run = subprocess.run([str(output / "check")], capture_output=True, text=True)
    (output / "check.log").write_text(run.stdout + run.stderr)
    paths = [p, k, gpu, provider / "drivers/iommu/arm/arm-smmu/arm-smmu-qcom.h", provider / "include/linux/adreno-smmu-priv.h", source / "kgsl_mmu.h", source / "kgsl_iommu.h", harness, Path(__file__)]
    result = dict(status="host-tested-not-hardware-validated" if run.returncode == 0 else "failed", command=cmd, exit=run.returncode, result=run.stdout + run.stderr,
                  inputs={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
                  extracted={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in output.glob("*.h")})
    (output / "RESULT.json").write_text(json.dumps(result, indent=2) + "\n")
    run.check_returncode()
    print(run.stdout, end="")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--provider", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--kernel", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    a = parser.parse_args()
    check(a.provider.resolve(), a.source.resolve(), a.kernel.resolve(), a.output.absolute())
