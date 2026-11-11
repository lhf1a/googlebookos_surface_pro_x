#!/usr/bin/env python3
"""Test native GPU identity/IRQ, GMU binding, and actual 6.18 registration rollback."""
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
    pattern = (r"^(?:static )?(?:(?:int|void) (?:__init |__exit )?|"
               r"(?:static )?const struct adreno_gpu_core \*\n)" +
               re.escape(name) + r"\([^;{]*\)\n\{.*?^\}")
    result = re.findall(pattern, code, re.M | re.S)
    if len(result) != 1:
        raise ValueError("Expected one function: " + name)
    return result[0]


def check(source, kernel, dtb, output):
    output.mkdir(parents=True, exist_ok=False)
    functions, hashes = [], {}
    for path, names in (
        (source / "adreno.c", ("adreno_get_chipid", "adreno_identify_gpu", "adreno_bind_gmu", "adreno_unbind_gmu")),
        (source / "kgsl.c", ("kgsl_request_gpu_irq",)),
        (kernel / "drivers/base/platform.c", ("__platform_register_drivers", "platform_unregister_drivers")),
        (source / "kgsl_gmu_core.c", ("gmu_core_register", "gmu_core_unregister")),
        (source / "adreno.c", ("kgsl_3d_init", "kgsl_3d_exit")),
    ):
        code = path.read_text()
        functions.extend(extract(code, name) for name in names)
        hashes[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
    core = (source / "kgsl_gmu_core.c").read_text()
    array = re.search(r"^static struct platform_driver \* const gmu_drivers\[\] = \{.*?^\};", core, re.M | re.S)
    assert array
    adreno = (source / "adreno.c").read_text()
    match = re.search(r"^static const struct of_device_id adreno_match_table\[\] = \{.*?^\};", adreno, re.M | re.S)
    assert match
    body = module.extract(adreno, "adreno_device_probe")
    assert "of_find_matching_node(dev->of_node" not in adreno
    assert "status = adreno_bind_gmu(device);\n\tif (status)\n\t\tgoto err;" in body
    cleanup = body[body.index("\nerr:"):]
    assert cleanup.index("adreno_unbind_gmu(device)") < cleanup.index("device->pdev = NULL")
    assert "dev_set_drvdata(dev, NULL)" in cleanup
    body = module.extract((source / "kgsl.c").read_text(), "kgsl_device_platform_probe")
    assert "status = kgsl_request_gpu_irq(device);" in body and "disable_irq(" not in body
    extracted = output / "kgsl-gpu-probe-functions.h"
    extracted.write_text(array[0] + "\n" + match[0] + "\n\n" + "\n\n".join(functions) + "\n")
    compatibles = subprocess.check_output(["fdtget", "-t", "s", str(dtb), "/soc/gpu@2c00000", "compatible"], text=True).split()
    assert "qcom,adreno-680.1" in compatibles
    (output / "kgsl-gpu-probe-target.h").write_text("static const char *target_compatibles[] = {" + ",".join(json.dumps(c) for c in compatibles) + "};\n")
    harness = ROOT / "tests/kgsl_gpu_probe_test.c"
    command = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-sign-compare",
               "-fsanitize=address,undefined", "-fno-pie", "-no-pie", "-I", str(output), str(harness), "-o", str(output / "check")]
    subprocess.run(command, check=True)
    result = subprocess.run([str(output / "check")], capture_output=True, text=True)
    (output / "check.log").write_text(result.stdout + result.stderr)
    report = dict(status="host-tested-not-hardware-validated" if result.returncode == 0 else "failed", source_sha256=hashes,
                  command=command, exit=result.returncode, harness_sha256=hashlib.sha256(harness.read_bytes()).hexdigest(),
                  checker_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  dtb_sha256=hashlib.sha256(dtb.read_bytes()).hexdigest(),
                  extracted_sha256=hashlib.sha256(extracted.read_bytes()).hexdigest(), result=result.stdout + result.stderr)
    (output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")
    result.check_returncode()
    print(result.stdout, end="")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--kernel-source", type=Path, required=True)
    parser.add_argument("--dtb", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    check(args.source.resolve(), args.kernel_source.resolve(), args.dtb.resolve(), args.output.absolute())
