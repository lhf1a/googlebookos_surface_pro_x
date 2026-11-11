#!/usr/bin/env python3
"""Check real branch retention registration, register writes and PM error paths."""
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
    branch = (source / "drivers/clk/qcom/clk-branch.c").read_text()
    start = branch.index("/* Only registered branch2 clocks")
    code = branch[start:] + "\n" + extract.extract((source / "drivers/clk/qcom/clk-regmap.c").read_text(), "devm_clk_register_regmap")
    (output / "retention-functions.h").write_text(code)
    public = (source / "include/linux/clk/qcom.h").read_text()
    branch_h = (source / "drivers/clk/qcom/clk-branch.h").read_text()
    types = re.search(r"enum branch_mem_flags \{.*?\};", public, re.S).group(0)
    for name in ("CBCR_FORCE_MEM_CORE_ON", "CBCR_FORCE_MEM_PERIPH_ON", "CBCR_FORCE_MEM_PERIPH_OFF"):
        types += "\n" + re.search(r"^#define " + name + r"\s+[^\n]+", branch_h, re.M).group(0)
    (output / "retention-types.h").write_text(types + "\n")
    harness = ROOT / "tests/kgsl_clock_retention_test.c"
    cmd = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
           "-pthread", "-fsanitize=address,undefined", "-fno-pie", "-no-pie", "-I", str(output), str(harness), "-o", str(output / "check")]
    subprocess.run(cmd, check=True)
    run = subprocess.run([str(output / "check")], capture_output=True, text=True, timeout=20)
    (output / "check.log").write_text(run.stdout + run.stderr)
    names = ("drivers/clk/qcom/clk-branch.c", "drivers/clk/qcom/clk-branch.h", "drivers/clk/qcom/clk-regmap.c", "include/linux/clk/qcom.h")
    report = dict(status="host-tested-not-hardware-validated" if run.returncode == 0 else "failed", exit=run.returncode,
                  source_sha256={n: hashlib.sha256((source/n).read_bytes()).hexdigest() for n in names}, command=cmd,
                  harness_sha256=hashlib.sha256(harness.read_bytes()).hexdigest(), checker_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  extracted_sha256={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in output.glob("*.h")}, result=run.stdout+run.stderr)
    (output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")
    run.check_returncode()
    print(run.stdout, end="")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    check(args.source.resolve(), args.output.absolute())
