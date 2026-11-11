#!/usr/bin/env python3
"""Check GPU child-node ownership and the actual early-probe/common-error slices."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("extract", ROOT / "scripts/check-kgsl-dmabuf.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def extract(code, name):
    match = re.findall(r"^(?:static )?(?:inline )?(?:(?:int|void|bool)\s+|(?:const )?struct (?:device_node|adreno_gpu_core) \*\s*)" + re.escape(name) + r"\([^;{]*\)\n\{.*?^\}", code, re.M | re.S)
    if len(match) != 1:
        raise ValueError("Expected one definition: " + name)
    return match[0]


def check(source, kernel, output):
    output.mkdir(parents=True, exist_ok=False)
    code = (source / "adreno.c").read_text()
    assert "of_find_node_by_name(" not in code
    of_path = kernel / "drivers/of/base.c"
    functions = [extract(of_path.read_text(), "of_get_child_by_name")]
    names = ("adreno_get_gpu_model_node", "_rev_match", "_get_gpu_core", "adreno_get_soc_hw_revision_node",
             "adreno_of_get_legacy_pwrlevels", "adreno_of_get_pwrlevels", "l3_pwrlevel_probe")
    functions += [extract(code, n) for n in names]
    # Preserve the actual prefix and common error path; the later GPU/MMU/GMU probe is not exercised.
    probe = module.extract(code, "adreno_device_probe")
    prefix = probe[:probe.index("\n\t/*\n\t * Bind the GMU components")]
    cleanup = probe[probe.index("\nerr:"):]
    functions.append(prefix.replace("adreno_device_probe(", "adreno_probe_prefix_test(", 1) +
                     "\n\t(void)priv; (void)size;\n\treturn 0;" + cleanup)
    header = output / "kgsl-dt-ownership-functions.h"
    header.write_text("\n\n".join(functions) + "\n")
    harness = ROOT / "tests/kgsl_dt_ownership_test.c"
    command = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-sign-compare",
               "-fsanitize=address,undefined", "-fno-pie", "-no-pie", "-I", str(output), str(harness), "-o", str(output / "check")]
    subprocess.run(command, check=True)
    result = subprocess.run([str(output / "check")], capture_output=True, text=True)
    (output / "check.log").write_text(result.stdout + result.stderr)
    report = dict(status="host-tested-not-hardware-validated" if result.returncode == 0 else "failed", command=command,
                  exit=result.returncode, source_sha256=hashlib.sha256((source / "adreno.c").read_bytes()).hexdigest(),
                  of_source_sha256=hashlib.sha256(of_path.read_bytes()).hexdigest(), harness_sha256=hashlib.sha256(harness.read_bytes()).hexdigest(),
                  checker_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), extracted_sha256=hashlib.sha256(header.read_bytes()).hexdigest(),
                  limits="OF iterator/property/ref leaves mocked; early probe only through bus init plus actual shared error block, not whole probe",
                  result=result.stdout + result.stderr)
    (output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")
    result.check_returncode()
    print(result.stdout, end="")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--kernel-source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    check(args.source.resolve(), args.kernel_source.resolve(), args.output.absolute())
