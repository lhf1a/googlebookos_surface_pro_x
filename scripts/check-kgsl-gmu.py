#!/usr/bin/env python3
"""Fault-inject prepared GMU allocation/mapping functions; no hardware execution."""
import argparse
import hashlib
import importlib.util
import json
import re
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("kgsl_check", ROOT / "scripts/check-kgsl-dmabuf.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def check(source, output):
    functions, hashes = [], {}
    for name, names in (
        ("adreno_a6xx_gmu.c", ("a6xx_gmu_map_sg", "reserve_gmu_kernel_block",
                               "a6xx_gmu_unmap_buffer", "a6xx_free_gmu_globals", "a6xx_gmu_iommu_init",
                               "a6xx_gmu_aop_send_acd_state", "set_acd",
                               "a6xx_gmu_acd_set", "a6xx_gmu_aop_qmp_init")),
        ("adreno_a6xx_hwsched_hfi.c", ("get_attrs", "gmu_import_buffer",
            "lookup_mem_alloc_table", "get_mem_alloc_entry", "process_mem_alloc",
            "a6xx_hwsched_free_gmu_imports")),
        ("kgsl_mmu.c", ("kgsl_mmu_put_gpuaddr",)),
        ("kgsl_sharedmem.c", ("kgsl_sharedmem_free",))):
        path = source / name
        source_text = path.read_text()
        for f in names:
            if f in ("lookup_mem_alloc_table", "get_mem_alloc_entry"):
                start = source_text.index("static struct mem_alloc_entry *" + f + "(")
                end = source_text.index("\nstatic ", start + 1)
                body = source_text[start:end].rstrip()
                assert body.endswith("\n}")
                functions.append(body)
            else:
                functions.append(module.extract(source_text, f))
        hashes[name] = hashlib.sha256(path.read_bytes()).hexdigest()
    output.mkdir(parents=True, exist_ok=False)
    definitions = []
    for name, prefix in (("kgsl.h", "KGSL_MEMDESC_"),
                         ("adreno_a6xx_hwsched_hfi.h", "MEMFLAG_")):
        path = source / name
        definitions.extend(re.findall(r"^#define " + prefix + r"[^\n]+", path.read_text(), re.M))
        hashes[name] = hashlib.sha256(path.read_bytes()).hexdigest()
    constants = output / "kgsl-gmu-constants.h"
    constants.write_text("\n".join(definitions) + "\n")
    extracted = output / "kgsl-gmu-functions.h"
    extracted.write_text("\n\n".join(functions) + "\n")
    harness = ROOT / "tests/kgsl_gmu_test.c"
    command = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
               "-Wno-unused-parameter", "-Wno-sign-compare", "-fsanitize=address,undefined",
               "-fno-pie", "-no-pie", "-I", str(output), str(harness), "-o", str(output / "check")]
    subprocess.run(command, check=True)
    result = subprocess.run([str(output / "check")], capture_output=True, text=True)
    (output / "check.log").write_text(result.stdout + result.stderr)
    report = dict(status="host-tested-not-hardware-validated" if result.returncode == 0 else "failed",
                  source_sha256=hashes, command=command, exit=result.returncode,
                  harness_sha256=hashlib.sha256(harness.read_bytes()).hexdigest(),
                  constants_sha256=hashlib.sha256(constants.read_bytes()).hexdigest(),
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
