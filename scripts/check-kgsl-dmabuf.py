#!/usr/bin/env python3
"""Run ownership failure injection against the actual prepared KGSL import/destructor functions."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def extract(source, name):
    matches = list(re.finditer(r"^(?:static )?(?:inline )?(?:(?:int|void|unsigned long|long|bool|u32)\s+|u32\s*\*\s*|struct (?:dma_fence|gmu_memdesc|rpmh_bw_votes|kgsl_pagetable)\s*\*\s*)" + name + r"\([^;{]*\)\n\{.*?^\}",
                              source, re.M | re.S))
    if len(matches) != 1:
        raise ValueError("Expected exactly one definition: " + name)
    return matches[0].group(0)


def check(source, output, variant="oculus", kernel=None):
    if variant == "xiaomi" and kernel is None:
        raise ValueError("Xiaomi checks require the matching 6.18 --kernel-source for real fence semantics")
    code = source.read_text()
    functions = "\n\n".join(extract(code, name) for name in ("kgsl_destroy_ion", "kgsl_dmabuf_cachemode", "kgsl_setup_dma_buf"))
    output.mkdir(parents=True, exist_ok=False)
    iommu = source.with_name("kgsl_iommu.c")
    map_names = ("_iommu_map_single_page", "_iommu_map_guard_page", "_iommu_map_sg", "kgsl_iommu_map") if variant == "xiaomi" else ("_iommu_map_sg", "kgsl_iommu_map")
    mapping = "\n\n".join(extract(iommu.read_text(), name) for name in map_names)
    (output / "kgsl-functions.h").write_text(functions + "\n")
    (output / "kgsl-map-functions.h").write_text(mapping + "\n")
    cache = "\n\n".join(extract(code, name) for name in (
        "kgsl_dmabuf_sync_cache", "_kgsl_gpumem_sync_cache", "mem_id_cmp",
        "kgsl_ioctl_gpumem_sync_cache_bulk"))
    (output / "kgsl-cache-functions.h").write_text(cache + "\n")
    if variant == "xiaomi":
        userptr = "\n\n".join(extract(code, name) for name in (
            "check_vma", "memdesc_sg_virt", "kgsl_destroy_anon"))
        (output / "kgsl-userptr-functions.h").write_text(userptr + "\n")
        pool = source.with_name("kgsl_pool.c").read_text()
        (output / "kgsl-pool-functions.h").write_text("\n\n".join(
            extract(pool, name) for name in ("kgsl_probe_page_pools", "kgsl_exit_page_pools")) + "\n")
    kernel_hashes = {}
    if variant == "xiaomi":
        timeline = source.with_name("kgsl_timeline.c")
        snippets = []
        for path, names in (
            (kernel / "include/linux/dma-fence.h", ("__dma_fence_is_later",)),
            (kernel / "drivers/dma-buf/dma-fence.c", ("dma_fence_init64",))):
            text = path.read_text()
            kernel_hashes[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
            snippets.extend(extract(text, name) for name in names)
        (output / "kernel-fence-functions.h").write_text("\n\n".join(snippets) + "\n")
        (output / "kgsl-fence-functions.h").write_text("\n\n".join(
            extract(timeline.read_text(), name) for name in
            ("timeline_fence_signaled", "kgsl_timeline_fence_alloc")) + "\n")
    commands, results, hashes = [], [], {}
    names = ("import", "cache", "xiaomi_map", "userptr", "pool", "fence") if variant == "xiaomi" else ("import", "cache")
    for name in names:
        harness = ROOT / ("tests/kgsl_dmabuf_" + name + "_test.c")
        command = ["gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                   "-Wno-unused-parameter", "-Wno-sign-compare", "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                   "-I", str(output), str(harness), "-o", str(output / ("check-" + name))]
        if variant == "xiaomi":
            command.insert(1, "-DKGSL_XIAOMI")
        commands.append(command)
        hashes[str(harness.relative_to(ROOT))] = hashlib.sha256(harness.read_bytes()).hexdigest()
        subprocess.run(command, check=True)
        result = subprocess.run([str(output / ("check-" + name))], capture_output=True, text=True)
        (output / (name + ".log")).write_text(result.stdout + result.stderr)
        result.check_returncode()
        results.append(result.stdout)
    report = dict(status="host-ownership-cache-tested-not-hardware-validated", variant=variant, commands=commands,
                  source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                  iommu_source_sha256=hashlib.sha256(iommu.read_bytes()).hexdigest(),
                  harness_sha256=hashes, kernel_fence_sha256=kernel_hashes,
                  extracted_sha256={p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                    for p in sorted(output.glob("*functions.h"))}, result="".join(results))
    (output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")
    print(report["result"], end="")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="Prepared drivers/gpu/msm/kgsl.c")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--variant", choices=("oculus", "xiaomi"), default="oculus")
    parser.add_argument("--kernel-source", type=Path, help="Matching 6.18 tree, required for Xiaomi fence checks")
    args = parser.parse_args()
    check(args.source.resolve(), args.output.absolute(), args.variant, args.kernel_source)
