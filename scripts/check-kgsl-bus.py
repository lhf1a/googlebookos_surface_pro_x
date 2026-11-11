#!/usr/bin/env python3
"""Check actual KGSL bus validation, votes and caller retries; no hardware execution."""
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
        ("adreno.c", ("adreno_interconnect_bus_set",)),
        ("adreno_a6xx_gmu.c", ("a6xx_gmu_bus_set",)),
        ("adreno_a6xx_hwsched.c", ("a6xx_hwsched_bus_set",)),
        ("kgsl_bus.c", ("_ab_buslevel_update", "kgsl_bus_update", "validate_pwrlevels",
                        "kgsl_bus_get_opp_table", "kgsl_bus_read_policy_u32", "kgsl_bus_get_indexed_table",
                        "kgsl_bus_get_native_table", "kgsl_bus_init", "kgsl_bus_close")),
        ("kgsl_pwrctrl.c", ("kgsl_pwrctrl_axi",)),
        ("kgsl_pwrscale.c", ("kgsl_busmon_target",)),
    ):
        path = source / name
        for function in names:
            body = path.read_text()
            if function in ("kgsl_bus_read_policy_u32", "kgsl_bus_get_indexed_table", "kgsl_bus_get_native_table") and function + "(" not in body:
                continue
            functions.append(module.extract(body, function))
        hashes[name] = hashlib.sha256(path.read_bytes()).hexdigest()
    functions.insert(0, module.extract((source / "kgsl_bus.c").read_text(), "kgsl_bus_set_icc"))
    output.mkdir(parents=True, exist_ok=False)
    (output / "kgsl-bus-functions.h").write_text("\n\n".join(functions) + "\n")
    harness = ROOT / "tests/kgsl_bus_test.c"
    command = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
               "-Wno-unused-parameter", "-Wno-unused-function", "-fsanitize=address,undefined", "-fno-pie", "-no-pie", "-I", str(output),
               str(harness), "-o", str(output / "check")]
    subprocess.run(command, check=True)
    result = subprocess.run([str(output / "check")], capture_output=True, text=True)
    (output / "check.log").write_text(result.stdout + result.stderr)
    report = dict(status="host-tested-not-hardware-validated" if result.returncode == 0 else "failed",
                  source_sha256=hashes, command=command, exit=result.returncode,
                  harness_sha256=hashlib.sha256(harness.read_bytes()).hexdigest(),
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
