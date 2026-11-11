#!/usr/bin/env python3
"""Check actual KGSL/6.18 SCM calls; firmware transport and availability are mocked."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def check(source, kernel, output):
    spec = importlib.util.spec_from_file_location("extract", ROOT / "scripts/check-kgsl-dmabuf.py")
    extract = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(extract)
    output.mkdir(parents=True, exist_ok=False)
    scm = kernel / "drivers/firmware/qcom/qcom_scm.c"
    private = kernel / "drivers/firmware/qcom/qcom_scm.h"
    smccc = kernel / "include/linux/arm-smccc.h"
    inputs = [(scm, ("qcom_scm_set_gpu_smmu_aperture", "qcom_scm_set_gpu_smmu_aperture_is_available", "qcom_scm_set_remote_state")),
              (source / "kgsl_iommu.c", ("set_smmu_aperture",)),
              (source / "adreno_a5xx.c", ("a5xx_zap_shader_resume",))]
    defines = []
    for path, name in ((scm, "QCOM_SCM_CP_APERTURE_CONTEXT_MASK"), (smccc, "ARM_SMCCC_OWNER_SIP")):
        defines.append(re.search(r"^#define " + name + r"\s+[^\n]+", path.read_text(), re.M).group(0))
    (output / "scm-private.h").write_bytes(private.read_bytes())
    functions = [extract.extract(path.read_text(), name) for path, names in inputs for name in names]
    (output / "scm-functions.h").write_text("\n".join(defines + functions) + "\n")
    harness = ROOT / "tests/kgsl_scm_api_test.c"
    cmd = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-sign-compare",
           "-fsanitize=address,undefined", "-fno-pie", "-no-pie", "-I", str(output), str(harness), "-o", str(output / "check")]
    subprocess.run(cmd, check=True)
    run = subprocess.run([str(output / "check")], capture_output=True, text=True)
    (output / "check.log").write_text(run.stdout + run.stderr)
    paths = [p for p, _ in inputs] + [private, smccc, harness, Path(__file__)]
    report = dict(status="host-tested-not-firmware-validated" if run.returncode == 0 else "failed", exit=run.returncode,
                  command=cmd, result=run.stdout + run.stderr,
                  source_sha256={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
                  extracted_sha256={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in output.glob("*.h")})
    (output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")
    run.check_returncode()
    print(run.stdout, end="")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--kernel", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    check(args.source.resolve(), args.kernel.resolve(), args.output.absolute())
