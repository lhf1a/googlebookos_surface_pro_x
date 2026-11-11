#!/usr/bin/env python3
"""Run actual SCM DCVS and KGSL governor functions with mocked firmware/devfreq."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("extractor", ROOT / "scripts/check-kgsl-dmabuf.py")
extractor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(extractor)


def check(provider, source, output):
    output.mkdir(parents=True, exist_ok=False)
    provider_c = provider / "drivers/firmware/qcom/qcom_scm.c"
    provider_h = provider / "drivers/firmware/qcom/qcom_scm.h"
    governor = source / "governor_msm_adreno_tz.c"
    header = provider_h.read_text()
    types = header[:header.index("int qcom_scm_wait_for_wq_completion")]
    types += "\n#endif\n" + "\n".join(line for line in header.splitlines() if line.startswith("#define QCOM_SCM_") and "DCVS" in line)
    (output / "scm-types.h").write_text(types + "\n")
    selected = {
        "provider": (provider_c, ("qcom_scm_gpu_dcvs_buffer", "qcom_scm_gpu_dcvs_init", "qcom_scm_gpu_dcvs_init_ca", "qcom_scm_gpu_dcvs_update", "qcom_scm_gpu_dcvs_reset")),
        "governor": (governor, ("__secure_tz_reset_entry2", "__secure_tz_update_entry3", "tz_init", "devfreq_get_freq_level", "tz_get_target_freq", "tz_start", "tz_suspend")),
    }
    results = []
    for name, (path, functions) in selected.items():
        extracted = output / (name + "-functions.h")
        extracted.write_text("\n\n".join(extractor.extract(path.read_text(), f) for f in functions) + "\n")
        harness = ROOT / ("tests/kgsl_dcvs_" + name + "_test.c")
        command = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-sign-compare", "-fsanitize=address,undefined", "-fno-pie", "-no-pie", "-I", str(output), str(harness), "-o", str(output / name)]
        subprocess.run(command, check=True)
        result = subprocess.run([str(output / name)], capture_output=True, text=True)
        (output / (name + ".log")).write_text(result.stdout + result.stderr)
        results.append(dict(name=name, command=command, exit=result.returncode, output=result.stdout + result.stderr, source_sha256=hashlib.sha256(path.read_bytes()).hexdigest(), harness_sha256=hashlib.sha256(harness.read_bytes()).hexdigest(), extracted_sha256=hashlib.sha256(extracted.read_bytes()).hexdigest()))
        result.check_returncode()
        print(result.stdout, end="")
    report = dict(status="host-tested-not-firmware-or-hardware-validated", types_sha256=hashlib.sha256((output / "scm-types.h").read_bytes()).hexdigest(), provider_header_sha256=hashlib.sha256(provider_h.read_bytes()).hexdigest(), results=results)
    (output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--provider", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    check(args.provider.resolve(), args.source.resolve(), args.output.absolute())
