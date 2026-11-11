#!/usr/bin/env python3
"""Exercise real GMU resource functions against captured DT and mocked kernel APIs."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("extract", ROOT / "scripts/check-kgsl-dmabuf.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def check(source, dtb, output):
    output.mkdir(parents=True, exist_ok=False)
    functions, hashes = [], {}
    for name, names in (
        ("adreno_a6xx_gmu.c", ("a6xx_gmu_resource_fits", "a6xx_gmu_request_irq", "a6xx_gmu_reg_probe", "a6xx_load_pdc_ucode")),
        ("adreno.c", ("_compare_of", "_release_of", "adreno_add_gmu_components", "adreno_probe", "adreno_remove")),
    ):
        path = source / name
        functions.extend(module.extract(path.read_text(), n) for n in names)
        hashes[name] = hashlib.sha256(path.read_bytes()).hexdigest()
    macros = [line for line in (source / "a6xx_reg.h").read_text().splitlines() if line.startswith("#define PDC_")]
    macros += [line for line in (source / "adreno_a6xx_gmu.c").read_text().splitlines() if line.startswith("#define PDC_CMD_OFFSET")]
    assert len(macros) >= 27
    extracted = output / "kgsl-gmu-resources-functions.h"
    extracted.write_text("\n".join(macros + functions) + "\n")
    def prop(node, name, kind="x"):
        return subprocess.check_output(["fdtget", "-t", kind, str(dtb), node, name], text=True).split()
    gpu, gmu = "/soc/gpu@2c00000", "/soc/gmu@2c6a000"
    assert prop(gpu, "qcom,gmu") == prop(gmu, "phandle")
    assert "qcom,adreno-gmu-680.1" in prop(gmu, "compatible", "s")
    names = prop(gmu, "reg-names", "s")
    reg = [int(x, 16) for x in prop(gmu, "reg")]
    assert names == ["gmu", "gmu_pdc", "gmu_pdc_seq"] and len(reg) == 12
    ranges = [(reg[i] << 32 | reg[i+1], reg[i+2] << 32 | reg[i+3]) for i in range(0, 12, 4)]
    irq_names = prop(gmu, "interrupt-names", "s")
    assert irq_names == ["hfi", "gmu"]
    fixture = "static const struct resource target_resources[] = {\n"
    fixture += "".join('{%dULL, %dULL, %s},\n' % (start, start+size-1, json.dumps(name)) for name, (start, size) in zip(names, ranges))
    fixture += "};\n"
    gpu_reg = [int(x, 16) for x in prop(gpu, "reg")]
    fixture += "#define TARGET_GPU_BASE %dULL\n" % (gpu_reg[0] << 32 | gpu_reg[1])
    (output / "kgsl-gmu-resources-target.h").write_text(fixture)
    harness = ROOT / "tests/kgsl_gmu_resources_test.c"
    command = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
               "-Wno-unused-parameter", "-Wno-sign-compare", "-fsanitize=address,undefined",
               "-fno-pie", "-no-pie", "-I", str(output), str(harness), "-o", str(output / "check")]
    subprocess.run(command, check=True)
    result = subprocess.run([str(output / "check")], capture_output=True, text=True)
    (output / "check.log").write_text(result.stdout + result.stderr)
    report = dict(status="host-tested-not-hardware-validated" if result.returncode == 0 else "failed",
                  source_sha256=hashes, dtb_sha256=hashlib.sha256(dtb.read_bytes()).hexdigest(),
                  target_resources=dict(zip(names, ranges)), target_interrupts=prop(gmu, "interrupts"),
                  command=command, exit=result.returncode,
                  harness_sha256=hashlib.sha256(harness.read_bytes()).hexdigest(),
                  extracted_sha256=hashlib.sha256(extracted.read_bytes()).hexdigest(),
                  register_header_sha256=hashlib.sha256((source / "a6xx_reg.h").read_bytes()).hexdigest(),
                  result=result.stdout + result.stderr)
    (output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")
    result.check_returncode()
    print(result.stdout, end="")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--dtb", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    check(args.source.resolve(), args.dtb.resolve(), args.output.absolute())
