#!/usr/bin/env python3
"""Run unchanged SDE/DRM functions against pthread and ownership models.

--source accepts the prepared source root or its techpack/display/msm directory.
Use that source's include/uapi/drm/msm_drm.h, or pass --uapi-header explicitly
when checking a small source fragment. --kernel-source is the paired kernel.
This is a host regression, not a kernel build, hardware or retirement test.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
MODES = (
    "empty", "serial", "race-zero", "pinned-final-put", "partial-final-put",
    "alloc-fail", "zero-second", "attach-error", "callback-object",
    "callback-unregister", "register-alloc-fail", "attach-success", "null",
    "map-short-local", "map-short-import", "map-DMA0", "map-overflow",
    "map-zero-length", "map-discontiguous", "map-contiguous", "map-provider-error",
)
UAPI_DEFINES = ("MSM_BO_WC", "MSM_BO_UNCACHED", "MSM_BO_CACHED",
                "MSM_BO_CACHE_MASK", "MSM_MADV_WILLNEED")
PRIVATE_DEFINES = ("MSM_BO_STOLEN", "MSM_BO_KEEPATTRS", "MSM_BO_SKIPSYNC", "MSM_BO_EXTBUF")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def extract(text, name):
    pattern = (r"^(?:static )?(?:inline )?(?:struct [a-z_]+ \*|void \*|void|int|"
               r"dma_addr_t|uint64_t|vm_fault_t|bool|unsigned long)\s*\n?" +
               re.escape(name) + r"\([^;{]*\)\n\{.*?^\}")
    matches = list(re.finditer(pattern, text, re.M | re.S))
    if len(matches) != 1:
        raise ValueError(f"Expected one actual function {name}, found {len(matches)}")
    return matches[0].group()


def extract_define(text, name):
    lines = text.splitlines(keepends=True)
    hits = [i for i, line in enumerate(lines)
            if re.match(r"^#define\s+" + re.escape(name) + r"(?:\s|$)", line)]
    if len(hits) != 1:
        raise ValueError(f"Expected one actual define {name}, found {len(hits)}")
    start = end = hits[0]
    while lines[end].rstrip().endswith("\\"):
        end += 1
        if end == len(lines):
            raise ValueError(f"Truncated define {name}")
    return "".join(lines[start:end + 1]).rstrip("\r\n")


def source_paths(source, uapi_header=None):
    source = source.resolve()
    if (source / "msm_gem.c").is_file():
        msm = source
        source_root = source.parents[2] if len(source.parents) >= 3 else source
    else:
        source_root = source
        msm = source / "techpack/display/msm"
    uapi = (uapi_header.resolve() if uapi_header is not None
            else source_root / "include/uapi/drm/msm_drm.h")
    for path in (msm / "msm_gem.c", msm / "msm_gem_vma.c", msm / "msm_gem_prime.c",
                 msm / "msm_gem.h", msm / "msm_drv.h", uapi):
        if not path.is_file():
            raise ValueError(f"Missing actual source/UAPI: {path}; use --uapi-header for a fragment")
    return msm, uapi


def timeout_text(value):
    return value.decode(errors="replace") if isinstance(value, bytes) else (value or "")


def check(source, kernel_source, output, modes, uapi_header=None, compiler="gcc", timeout=6):
    source = source.resolve()
    kernel_source = kernel_source.resolve()
    output = output.absolute()
    msm, uapi = source_paths(source, uapi_header)
    gem = msm / "msm_gem.c"
    vma = msm / "msm_gem_vma.c"
    prime = msm / "msm_gem_prime.c"
    core = kernel_source / "drivers/gpu/drm/drm_prime.c"
    gem_core = kernel_source / "drivers/gpu/drm/drm_gem.c"
    order = [
        (core, ("drm_gem_is_prime_exported_dma_buf",)),
        (gem, ("msm_gem_publish",)),
        (gem, ("msm_gem_new_impl", "msm_gem_import_active", "msm_gem_import",
               "msm_gem_delayed_import", "add_vma", "lookup_vma", "del_vma")),
        (vma, ("msm_gem_address_space_destroy", "msm_gem_address_space_put",
               "smmu_aspace_map_vma", "smmu_aspace_unmap_vma")),
        (gem, ("put_iova", "msm_gem_get_iova_locked_active",
               "msm_gem_get_iova_locked", "msm_gem_pin_iova_active",
               "msm_gem_pin_iova", "msm_gem_get_and_pin_iova",
               "msm_gem_vunmap_locked", "get_vaddr", "msm_gem_put_vaddr",
               "msm_gem_free_object")),
        (prime, ("msm_gem_prime_import_sg_table",)),
        (core, ("drm_gem_prime_import_dev", "drm_prime_gem_destroy")),
        (prime, ("msm_gem_prime_import_active", "msm_gem_prime_import",
                  "msm_gem_prime_vmap", "msm_gem_prime_vunmap")),
        (gem_core, ("drm_gem_vmap_locked", "drm_gem_vunmap_locked",
                    "drm_gem_vmap", "drm_gem_vunmap")),
        (core, ("drm_gem_prime_handle_to_dmabuf",)),
        (gem, ("msm_gem_get_iova",)),
        (vma, ("smmu_aspace_register_cb", "smmu_aspace_unregister_cb")),
        (gem, ("msm_gem_aspace_domain_attach_detach_update",)),
    ]
    # Extract before creating the output: missing/ambiguous source cannot produce
    # a misleading partial test result. Function bodies are never rewritten.
    functions = [extract(path.read_text(), name) for path, names in order for name in names]
    definitions = [extract_define(uapi.read_text(), name) for name in UAPI_DEFINES]
    definitions += [extract_define((msm / "msm_gem.h").read_text(), name)
                    for name in PRIVATE_DEFINES]
    if re.search(r"^int msm_gem_aspace_domain_attach_detach_update", gem.read_text(), re.M):
        definitions.append("#define UPDATE_RETURNS_INT 1")
    output.mkdir(parents=True, exist_ok=False)
    extracted = output / "actual-functions.h"
    extracted.write_text("\n\n".join(functions) + "\n")
    definitions_path = output / "actual-definitions.h"
    definitions_path.write_text("\n".join(definitions) + "\n")
    harness = ROOT / "tests/sde_active_lock_test.c"
    compile_command = [compiler, "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                       "-Wno-unused-function", "-Wno-unused-parameter",
                       "-fsanitize=address,undefined", "-fno-pie", "-no-pie", "-pthread",
                       "-I", str(output), str(harness), "-o", str(output / "check")]
    commands = [("compile", compile_command)] + [(mode, [str(output / "check"), mode]) for mode in modes]
    env = {**os.environ, "ASAN_OPTIONS": "detect_leaks=1:halt_on_error=1",
           "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"}
    runs = []
    for label, command in commands:
        deadline = max(60, timeout) if label == "compile" else timeout
        try:
            result = subprocess.run(command, capture_output=True, text=True, timeout=deadline, env=env)
            rec = dict(label=label, command=command, exit_status=result.returncode,
                       stdout=result.stdout, stderr=result.stderr)
        except subprocess.TimeoutExpired as error:
            rec = dict(label=label, command=command, exit_status=124,
                       stdout=timeout_text(error.stdout), stderr=timeout_text(error.stderr),
                       timeout_seconds=deadline)
        runs.append(rec)
        (output / (label + ".stdout.txt")).write_text(rec["stdout"])
        (output / (label + ".stderr.txt")).write_text(rec["stderr"])
        print(label, rec["exit_status"], rec["stdout"].strip(), rec["stderr"][:160].strip(), flush=True)
        if label == "compile" and rec["exit_status"]:
            break
    success = len(runs) == len(commands) and all(rec["exit_status"] == 0 for rec in runs)
    inputs = {path for path, _ in order} | {msm / "msm_gem.h", msm / "msm_drv.h", uapi,
                                          harness, Path(__file__), extracted, definitions_path}
    report = dict(status="host-tested-not-device-validated" if success else "failed",
                  source=str(source), kernel_source=str(kernel_source), uapi_header=str(uapi),
                  cases=list(modes), functions=[name for _, names in order for name in names],
                  inputs={str(path): sha(path) for path in sorted(inputs)},
                  definitions=definitions, runs=runs, all_pass=success,
                  environment={key: env[key] for key in ("ASAN_OPTIONS", "UBSAN_OPTIONS")},
                  hardware_validated=False, kernel_built=False,
                  limits=["Actual production/core function bodies execute unchanged; BO/MADV defines are extracted from the specified UAPI.",
                          "pthread/reference/DMA/allocation leaves and enclosing types are models, not actual kernel concurrency or retirement.",
                          "Legacy private ION/DMA attribute values are opaque model tags, not numeric ABI assertions.",
                          "A timeout is a failed test, never a successful detach.",
                          "Old code cannot reach the two introduced snapshot-pin handshakes: those timeouts are not separate deadlock proofs.",
                          "Snapshot pinning does not close cross-provider map admission or prove hardware retirement."])
    (output / "RESULT.json").write_text(json.dumps(report, indent=2) + "\n")
    return 0 if success else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--kernel-source", type=Path, required=True)
    parser.add_argument("--uapi-header", type=Path)
    parser.add_argument("--output", type=Path, required=True, help="Fresh directory; existing outputs are never overwritten")
    parser.add_argument("--case", action="append", choices=MODES)
    parser.add_argument("--compiler", default="gcc")
    parser.add_argument("--timeout", type=float, default=6, help="Positive per-scenario timeout in seconds")
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    return check(args.source, args.kernel_source, args.output, args.case or MODES,
                 args.uapi_header, args.compiler, args.timeout)


if __name__ == "__main__":
    raise SystemExit(main())
