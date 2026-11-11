#!/usr/bin/env python3
"""Execute prepared GMU firmware parsing/loading with modeled DMA and MMIO."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("kgsl_extract", ROOT / "scripts/check-kgsl-dmabuf.py")
extractor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(extractor)


def check(source, output, firmware=None):
    files = {name: (source / name).read_text() for name in
             ("adreno_a6xx_gmu.c", "adreno_a6xx_gmu.h", "a6xx_reg.h", "kgsl_gmu_core.h")}
    header = files["adreno_a6xx_gmu.h"]
    types = []
    for name in ("gmu_block_header", "gmu_memdesc", "gmu_vma_entry"):
        types.append(re.search(r"struct " + name + r" \{.*?\n};", header, re.S).group())
    types.append(re.search(r"enum gmu_mem_type \{.*?\n};", header, re.S).group())
    types.extend(line for line in header.splitlines()
                 if line.startswith("#define GMU_BLK_TYPE_") or line.startswith("#define MAX_GMUFW_SIZE"))
    types.extend(line for line in files["a6xx_reg.h"].splitlines()
                 if line.startswith("#define A6XX_GMU_CM3_ITCM_START") or
                 line.startswith("#define A6XX_GMU_CM3_DTCM_START"))
    types.append(re.search(r"enum gmu_core_flags \{.*?\n};", files["kgsl_gmu_core.h"], re.S).group())
    for table in ("a6xx_gmu_vma_legacy", "a6xx_gmu_vma"):
        types.append(re.search(r"static const struct gmu_vma_entry " + table +
                               r"\[GMU_MEM_TYPE_MAX\] = \{.*?\n};",
                               files["adreno_a6xx_gmu.c"], re.S).group())
    assert "struct gmu_vma_entry vma[GMU_MEM_TYPE_MAX];" in header
    probe = extractor.extract(files["adreno_a6xx_gmu.c"], "a6xx_gmu_probe")
    assert probe.index("ret = a6xx_gmu_vma_init(adreno_dev);\n\tif (ret)\n\t\treturn ret;") < probe.index("gmu->pdev = pdev;")
    functions = [extractor.extract(files["adreno_a6xx_gmu.c"], name) for name in
                 ("a6xx_gmu_vma_init", "find_gmu_memdesc", "find_vma_block", "a6xx_gmu_next_fw_block",
                  "a6xx_gmu_validate_fw", "_load_legacy_gmu_fw", "load_tcm",
                  "a6xx_gmu_load_fw", "a6xx_gmu_process_prealloc", "a6xx_gmu_parse_fw")]
    output.mkdir(parents=True, exist_ok=False)
    (output / "firmware-types.h").write_text("\n\n".join(types) + "\n")
    (output / "firmware-functions.h").write_text("\n\n".join(functions) + "\n")
    harness = ROOT / "tests/kgsl_firmware_test.c"
    command = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
               "-Wno-unused-parameter", "-Wno-sign-compare", "-fsanitize=address,undefined",
               "-fno-pie", "-no-pie", "-I", str(output), str(harness), "-o", str(output / "check")]
    compiled = subprocess.run(command, capture_output=True, text=True)
    (output / "compile.log").write_text(compiled.stdout + compiled.stderr)
    compiled.check_returncode()
    run = [str(output / "check")] + ([str(firmware)] if firmware else [])
    result = subprocess.run(run, capture_output=True, text=True,
                            env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1"})
    (output / "check.log").write_text(result.stdout + result.stderr)
    inputs = [source / name for name in files] + [harness, Path(__file__)]
    if firmware:
        inputs.append(firmware)
    report = dict(status="host-functions-not-hardware-validated", exit=result.returncode,
                  command=command, run=run, result=result.stdout + result.stderr,
                  input_sha256={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},
                  extracted_sha256={p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                    for p in output.glob("*.h")})
    (output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")
    result.check_returncode()
    print(result.stdout, end="")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--firmware", type=Path, help="Optional captured A680 image with six preallocations; never bundled")
    args = parser.parse_args()
    check(args.source.resolve(), args.output.absolute(), args.firmware.resolve() if args.firmware else None)
