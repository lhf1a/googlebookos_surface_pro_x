#!/usr/bin/env python3
"""Check native GMU genpd lifecycle with mocked PM provider transitions."""
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


def check(source, output):
    functions, hashes = [], {}
    for name, names in (
        ("adreno_a6xx_gmu.c", ("a6xx_gmu_domain_put", "a6xx_gmu_disable_clks", "a6xx_gmu_disable_cx", "a6xx_gmu_enable_gdsc", "a6xx_gmu_power_probe", "a6xx_gmu_unmap_buffer", "a6xx_free_gmu_globals", "a6xx_gmu_remove")),
    ):
        path = source / name
        functions.extend(module.extract(path.read_text(), f) for f in names)
        hashes[name] = hashlib.sha256(path.read_bytes()).hexdigest()
    output.mkdir(parents=True, exist_ok=False)
    extracted = output / "kgsl-genpd-functions.h"
    extracted.write_text("\n\n".join(functions) + "\n")
    harness = ROOT / "tests/kgsl_genpd_test.c"
    command = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
               "-Wno-unused-parameter", "-Wno-sign-compare", "-fsanitize=address,undefined",
               "-fno-pie", "-no-pie", "-I", str(output), str(harness), "-o", str(output / "check")]
    subprocess.run(command, check=True)
    result = subprocess.run([str(output / "check")], capture_output=True, text=True)
    (output / "check.log").write_text(result.stdout + result.stderr)
    report = dict(status="host-tested-not-hardware-validated" if result.returncode == 0 else "failed",
                  source_sha256=hashes, command=command, exit=result.returncode,
                  harness_sha256=hashlib.sha256(harness.read_bytes()).hexdigest(),
                  extracted_sha256=hashlib.sha256(extracted.read_bytes()).hexdigest(),
                  result=result.stdout + result.stderr)
    (output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")
    result.check_returncode()
    print(result.stdout, end="")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="Prepared drivers/gpu/msm directory")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    check(args.source.resolve(), args.output.absolute())
