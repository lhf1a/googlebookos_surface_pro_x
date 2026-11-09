#!/usr/bin/env python3
"""Build a native QTI Vulkan candidate and its private mapper5 readback harness."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
DONOR_SHA = "c6253f16ae790a5f845c563e2705e36a005aecf327ed312f7de4ddcbd0e60dce"
RENAMES = {"SQ2NativeBuffer_" + suffix: "AHardwareBuffer_" + suffix
           for suffix in ("getNativeHandle", "acquire", "release", "allocate")}
RENAMES["sq2_getmodule"] = "hw_get_module"
# The former wrapper supplied a TLS HOST_CACHED scope. Native gralloc must not
# load its synthetic AHB bridge just to resolve this renamed GSL import.
RENAMES["sq2_memory_alloc_pure_64"] = "gsl_memory_alloc_pure_64"
OLD_CONFIG = b"/data/sq2vk-v77t/\0"
NEW_CONFIG = b"/odm/lib64/\0"
CONFIG_OFFSET = 0x57277
HAL = "vulkan.qti-native.so"
RUNPATH = "/odm/lib64:/vendor/lib64/egl"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_script(name):
    spec = importlib.util.spec_from_file_location(name.replace("-", "_"), ROOT / "scripts" / (name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def restore_native_imports(data, config_directory=NEW_CONFIG, memory_policy=False):
    if hashlib.sha256(data).hexdigest() != DONOR_SHA:
        raise ValueError("Expected verified Linux 6.18 Direct Vulkan donor")
    patches = []
    for before, after in RENAMES.items():
        if memory_policy and before == "sq2_memory_alloc_pure_64":
            continue
        old, new = before.encode() + b"\0", after.encode() + b"\0"
        if len(old) != len(new) or data.count(old) != 1 or new in data:
            raise ValueError("Unexpected donor import layout: " + before)
        offset = data.index(old)
        data = data[:offset] + new + data[offset + len(old):]
        patches.append({"offset": hex(offset), "before": before, "after": after})
    if data[CONFIG_OFFSET:CONFIG_OFFSET + len(OLD_CONFIG)] != OLD_CONFIG:
        raise ValueError("Unexpected private config slot")
    if config_directory not in (NEW_CONFIG, b"/vendor/lib64/\0"):
        raise ValueError("Unexpected native config directory")
    data = data[:CONFIG_OFFSET] + config_directory.ljust(len(OLD_CONFIG), b"\0") + data[CONFIG_OFFSET + len(OLD_CONFIG):]
    patches.append({"offset": hex(CONFIG_OFFSET), "before": OLD_CONFIG[:-1].decode(), "after": config_directory[:-1].decode()})
    return data, patches


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--native", type=Path, required=True, help="Verified mapper5/v33 runtime")
    parser.add_argument("--cc", type=Path, required=True)
    parser.add_argument("--android-stub", type=Path, required=True)
    parser.add_argument("--memory-policy", action="store_true", help="Preserve the verified HOST_CACHED TLS scope with a native-only HAL policy adapter")
    parser.add_argument("--system-layout", action="store_true", help="Use normal vendor runtime/config paths; private runner is not applicable")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    runtime = args.runtime.resolve()
    native = args.native.resolve()
    output = args.output.resolve()
    if output.exists() or (ROOT / "out").resolve() not in output.parents:
        parser.error("Output must be fresh and below repository out/")
    load_script("build-quest2-native-gpu-probe").verify_runtime(runtime)
    helper = load_script("prepare-quest2-v77-vulkan")
    pins = json.loads((ROOT / "configs/quest2-native-vulkan-reference.json").read_text())
    for name, expected in pins["native_sha256"].items():
        if (native / name).is_symlink() or sha(native / name) != expected:
            parser.error("Native runtime input mismatch: " + name)
    donor = runtime / "vulkan-v77-bridge.so"
    config_directory = b"/vendor/lib64/\0" if args.system_layout else NEW_CONFIG
    runpath = "/vendor/lib64:/vendor/lib64/egl" if args.system_layout else RUNPATH
    patched, patches = restore_native_imports(donor.read_bytes(), config_directory, args.memory_policy)
    original_code = helper.code_sections(donor.read_bytes())
    donor_elf = helper.elf_info(donor)
    if donor_elf["needed"].count("libqti-mapper3-bridge.so") != 1:
        parser.error("Expected exact old view-adapter dependency")
    output.mkdir(parents=True)
    inputs, commands = {}, []
    def record(path):
        inputs[str(path)] = sha(path)
    def run(argv):
        commands.append(list(map(str, argv)))
        subprocess.run(commands[-1], check=True)
    record(donor)
    record(runtime / "SHA256SUMS")
    for name in pins["native_sha256"]:
        path = native / name
        record(path)
        shutil.copy2(path, output / name)
    (output / HAL).write_bytes(patched)
    policy_helper = "libquest2-native-vulkan-memory.so"
    if args.memory_policy:
        memory_source = ROOT / "probe/graphics/quest2-native-vulkan-memory.c"
        policy_source = ROOT / "probe/graphics/quest2-native-vulkan-policy.c"
        run([args.cc, "--target=aarch64-linux-android35", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
             "-fPIC", "-shared", memory_source, "-L", output, "-lgsl", "-Wl,--no-undefined",
             "-Wl,-soname," + policy_helper, "-Wl,-rpath," + runpath, "-o", output / policy_helper])
        donor_path = ("/vendor/lib64/" if args.system_layout else "/odm/lib64/") + HAL
        run([args.cc, "--target=aarch64-linux-android35", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
             "-fPIC", "-shared", "-pthread", "-I", args.android_stub,
             '-DSQ2_NATIVE_VULKAN_DONOR="' + donor_path + '"', policy_source,
             "-L", output, "-lquest2-native-vulkan-memory", "-ldl", "-Wl,--no-undefined,-Bsymbolic",
             "-Wl,-soname,vulkan.qti-policy.so", "-Wl,-rpath," + runpath,
             "-o", output / "vulkan.qti-policy.so"])
        for path in (memory_source, policy_source, ROOT / "probe/graphics/quest2-native-vulkan-memory.h"):
            record(path)
        run(["patchelf", "--add-needed", policy_helper, output / HAL])
    (output / "vulkan-hal-entry.txt").write_text(("vulkan.qti-policy.so" if args.memory_policy else HAL) + "\n")
    run(["patchelf", "--remove-needed", "libqti-mapper3-bridge.so", "--set-soname", HAL,
         "--set-rpath", runpath, output / HAL])
    final_elf = helper.elf_info(output / HAL)
    expected_needed = ([policy_helper] if args.memory_policy else []) + [x for x in donor_elf["needed"] if x != "libqti-mapper3-bridge.so"]
    if (helper.code_sections((output / HAL).read_bytes()) != original_code or
            final_elf["needed"] != expected_needed or
            final_elf["soname"] != HAL or final_elf["runpath"] != runpath):
        raise ValueError("Native Vulkan ELF verification failed")
    symbols = subprocess.check_output(["readelf", "--dyn-syms", "-W", output / HAL], text=True)
    undefined = {line.split()[7] for line in symbols.splitlines()
                 if len(line.split()) >= 8 and line.split()[6] == "UND"}
    private_imports = {name for name in undefined if name.startswith(("sq2_", "SQ2", "quest2_"))}
    expected_private = {"sq2_memory_alloc_pure_64"} if args.memory_policy else set()
    expected_native = set(RENAMES.values()) - ({"gsl_memory_alloc_pure_64"} if args.memory_policy else set())
    if private_imports != expected_private or not expected_native <= undefined:
        raise ValueError("Native Vulkan imports were not restored")
    for shader, symbol, header in (("vulkan-compute.comp", "sq2_compute", "vulkan-compute.h"),
                                   ("vulkan-raster.vert", "sq2_raster_vert", "vulkan-raster-vert.h"),
                                   ("vulkan-raster.frag", "sq2_raster_frag", "vulkan-raster-frag.h")):
        source = ROOT / "probe/graphics" / shader
        record(source)
        run(["glslangValidator", "-V", "--target-env", "vulkan1.1", "--vn", symbol,
             source, "-o", output / header])
    run([args.cc, "--target=aarch64-linux-android35", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
         "-I", output, "-I", args.android_stub, ROOT / "probe/graphics/vulkan-readback.c", "-ldl", "-landroid",
         "-o", output / "vulkan-readback"])
    run(["patchelf", "--replace-needed", "libandroid.so", "libnativewindow.so", output / "vulkan-readback"])
    run([args.cc, "--target=aarch64-linux-android35", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
         "-DSQ2_NATIVE_GRALLOC", ROOT / "probe/graphics/quest2-vulkan-window.c", "-landroid", "-lmediandk", "-ldl",
         "-o", output / "quest2-native-vulkan-window"])
    run(["patchelf", "--replace-needed", "libandroid.so", "libnativewindow.so", output / "quest2-native-vulkan-window"])
    record(ROOT / "probe/graphics/quest2-vulkan-window.c")
    runner = ROOT / "probe/graphics/quest2-native-vulkan-test.sh"
    shutil.copy2(runner, output / runner.name)
    record(runner)
    for name in ("vulkan-readback.c", "vulkan-ahb-readback.h", "vulkan-ahb-export.h",
                 "vulkan-raster-readback.h", "vulkan-memory-types.h"):
        record(ROOT / "probe/graphics" / name)
    for path in (Path(__file__), ROOT / "configs/quest2-native-vulkan-reference.json",
                 ROOT / "scripts/build-quest2-native-gpu-probe.py", ROOT / "scripts/prepare-quest2-v77-vulkan.py",
                 args.cc.resolve(), args.android_stub / "hardware/hwvulkan.h"):
        record(path)
    hashes = {path.name: sha(path) for path in sorted(output.iterdir())}
    (output / "SHA256SUMS").write_text("".join(f"{digest}  {name}\n" for name, digest in hashes.items()))
    (output / "BUILD.json").write_text(json.dumps({
        "status": "built-not-device-validated", "inputs": inputs, "commands": commands, "sha256": hashes,
        "memory_policy": args.memory_policy, "hal_entry": "vulkan.qti-policy.so" if args.memory_policy else HAL,
        "system_layout": args.system_layout, "config_directory": config_directory[:-1].decode(),
        "private_runner_applicable": not args.system_layout, "string_patches": patches, "output_elf": final_elf, "executable_sections_sha256": original_code,
        "executable_sections_unchanged": True, "native_ahb_and_gralloc_imports": True,
        "old_mapper_implementation_included": False, "old_host_cached_scope_preserved": args.memory_policy, "synthetic_handle_adapter_included": False,
        "tools": {tool: subprocess.check_output([tool, "--version"], text=True).splitlines()[0]
                  for tool in ("patchelf", "glslangValidator")},
        "limits": ["No runtime Vulkan, WSI, physical scanout, or performance claim until device evidence",
                   "Native memory policy needs device flags/type and cache-pressure validation" if args.memory_policy else
                   "Former HOST_CACHED TLS allocation override is not retained; cache-mode truthfulness must be resolved before default Vulkan enablement",
                   "Real donor HIDL fallback behavior remains to be tested against native gralloc",
                   "Probe uses private mapper5 Binder and mount namespace; normal desktop remains running",
                   "Legacy mapper interface libraries remain ELF dependencies; no old mapper implementation supplied"]
    }, indent=2) + "\n")
    print("QUEST2_NATIVE_VULKAN_BUILT", output)


if __name__ == "__main__":
    main()
