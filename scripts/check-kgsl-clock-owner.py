#!/usr/bin/env python3
"""Exercise actual KGSL power initialization; clock/DT/PM providers are mocked."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
ROOT = Path(__file__).resolve().parents[1]


def check(source, output):
    spec = importlib.util.spec_from_file_location("extract", ROOT / "scripts/check-kgsl-dmabuf.py")
    extract = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(extract)
    output.mkdir(parents=True, exist_ok=False)
    functions = []
    hashes = {}
    for filename, names in (("kgsl_gmu_core.c", ("gmu_core_gpmu_isenabled",)),
                            ("kgsl_pwrctrl.c", ("kgsl_pwrctrl_init",))):
        path = source / filename
        hashes[filename] = hashlib.sha256(path.read_bytes()).hexdigest()
        functions.extend(extract.extract(path.read_text(), name) for name in names)
    power = (source / "kgsl_pwrctrl.h").read_text()
    types = []
    for name in ("KGSL_MAX_PWRLEVELS", "KGSL_MAX_CLKS", "KGSL_PWRFLAGS_NAP_OFF", "KGSL_XO_CLK_FREQ", "KGSL_RBBMTIMER_CLK_FREQ"):
        types.append(re.search(r"^#define " + name + r"\s+[^\n]+", power, re.M).group(0))
    types.append(re.search(r"^struct kgsl_pwrlevel \{.*?^\};", power, re.M | re.S).group(0))
    (output / "clock-types.h").write_text("\n".join(types) + "\n")
    header = output / "clock-functions.h"
    header.write_text("\n\n".join(functions) + "\n")
    harness = ROOT / "tests/kgsl_clock_owner_test.c"
    cmd = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-sign-compare",
           "-fsanitize=address,undefined", "-fno-pie", "-no-pie", "-I", str(output), str(harness), "-o", str(output / "check")]
    subprocess.run(cmd, check=True)
    run = subprocess.run([str(output / "check")], capture_output=True, text=True)
    (output / "check.log").write_text(run.stdout + run.stderr)
    hashes["kgsl_pwrctrl.h"] = hashlib.sha256((source / "kgsl_pwrctrl.h").read_bytes()).hexdigest()
    report = dict(status="host-tested-not-hardware-validated" if run.returncode == 0 else "failed", exit=run.returncode,
                  source_sha256=hashes, command=cmd, result=run.stdout + run.stderr,
                  harness_sha256=hashlib.sha256(harness.read_bytes()).hexdigest(),
                  checker_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  extracted_sha256={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in output.glob("*.h")})
    (output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")
    run.check_returncode()
    print(run.stdout, end="")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    check(args.source.resolve(), args.output.absolute())
