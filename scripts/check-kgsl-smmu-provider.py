#!/usr/bin/env python3
"""Check real SMMU provider functions with injected PM/MMIO; no hardware proof."""
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


def check(source, kernel, output):
    provider = source / "drivers/iommu/arm/arm-smmu/arm-smmu-qcom.c"
    header = kernel / "drivers/iommu/arm/arm-smmu/arm-smmu.h"
    public = source / "include/linux/adreno-smmu-priv.h"
    pgtable = kernel / "include/linux/io-pgtable.h"
    text = header.read_text()
    constants = []
    for line in text.splitlines():
        if line.startswith("#define ") and any(word in line.split()[1] for word in
            ("ARM_SMMU_TCR_", "ARM_SMMU_TTBRn_ASID", "ARM_SMMU_CB_TTBR0",
             "ARM_SMMU_CB_CONTEXTIDR", "ARM_SMMU_CB_S1_TLBIASID", "ARM_SMMU_CB_TLBSYNC",
             "ARM_SMMU_CB_TLBSTATUS", "ARM_SMMU_sTLBGSTATUS_GSACTIVE", "TLB_LOOP_TIMEOUT", "TLB_SPIN_COUNT")):
            constants.append(line)
    constants.extend(line.strip() for line in pgtable.read_text().splitlines()
                     if line.strip().startswith("#define IO_PGTABLE_QUIRK_ARM_TTBR1"))
    functions = [module.extract(text, "arm_smmu_lpae_tcr")]
    for name in ("qcom_smmu_tlb_sync_checked", "qcom_smmu_tlb_sync",
                 "qcom_adreno_smmu_set_ttbr0_cfg", "qcom_adreno_smmu_get_context_bank",
                 "qcom_adreno_smmu_reset_ttbr0", "qcom_adreno_smmu_flush_tlb"):
        functions.append(module.extract(provider.read_text(), name))
    output.mkdir(parents=True, exist_ok=False)
    (output / "smmu-constants.h").write_text("\n".join(constants) + "\n")
    (output / "smmu-functions.h").write_text("\n\n".join(functions) + "\n")
    harness = ROOT / "tests/kgsl_smmu_provider_test.c"
    command = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
               "-fsanitize=address,undefined", "-fno-pie", "-no-pie", "-I", str(output),
               str(harness), "-o", str(output / "check")]
    subprocess.run(command, check=True)
    result = subprocess.run([str(output / "check")], capture_output=True, text=True)
    (output / "check.log").write_text(result.stdout + result.stderr)
    inputs = [provider, header, pgtable, public, harness, ROOT / "scripts/check-kgsl-dmabuf.py",
              Path(__file__).resolve(), output / "smmu-functions.h", output / "smmu-constants.h"]
    report = dict(status="host-tested-not-hardware-validated" if result.returncode == 0 else "failed",
                  inputs={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},
                  command=command, exit=result.returncode, result=result.stdout + result.stderr)
    (output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")
    result.check_returncode()
    print(result.stdout, end="")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="Prepared provider tree")
    parser.add_argument("--kernel-source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    check(args.source.resolve(), args.kernel_source.resolve(), args.output.absolute())
