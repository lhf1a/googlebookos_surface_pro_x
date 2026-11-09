#!/usr/bin/env python3
"""Build the actual source QTI HWC allocator with AHB/mapper5; never deploy or start HWC."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def prepare(source, output):
    config = json.loads((ROOT / "probe/graphics/sm8350-hwc-allocator-pins.json").read_text())
    patch = ROOT / config["patch"]
    if sha(patch) != config["patch_sha256"]:
        raise ValueError("Allocator patch mismatch")
    for name, pin in config["files"].items():
        path = source / name
        if path.is_symlink() or sha(path) != pin["original"]:
            raise ValueError("Unexpected allocator source: " + name)
    output.mkdir(parents=True, exist_ok=False)
    for name in config["files"]:
        target = output / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes((source / name).read_bytes())
    shutil.copytree(source / "sdm/include", output / "sdm/include", dirs_exist_ok=True)
    command = ["patch", "--batch", "--fuzz=0", "-p1", "-i", str(patch)]
    subprocess.run(command, cwd=output, check=True)
    for name, pin in config["files"].items():
        if sha(output / name) != pin["patched"]:
            raise ValueError("Patched allocator source mismatch: " + name)
    return config, command

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("source", "references", "output", "cc", "mapper-headers", "common-headers",
                 "binder-headers", "core-headers", "hardware-headers", "platform",
                 "donor", "source-libraries"):
        parser.add_argument("--" + name, type=Path, required=True)
    a = parser.parse_args()
    donor_pin = json.loads((ROOT / "configs/quest2-v77-display-reference.json").read_text())
    gralloc = a.donor / "lib64/libgrallocutils.so"
    if sha(gralloc) != donor_pin["files"]["/lib64/libgrallocutils.so"]:
        raise ValueError("Unexpected donor grallocutils")
    config, patch_command = prepare(a.source, a.output)
    flags = [str(a.cc), "--target=aarch64-linux-android35", "-std=c++20", "-O2", "-fPIC",
             "-fno-rtti", "-nostdlib++", "-Wall", "-Wextra", "-Werror",
             "-Wno-unused-parameter", "-Wno-missing-field-initializers",
             "-Wno-unqualified-std-cast-call", "-include",
             str(ROOT / "probe/graphics/quest2-platform-cxx.h")]
    for path in (a.mapper_headers, a.common_headers, a.binder_headers,
                 a.core_headers, a.hardware_headers):
        flags += ["-isystem", str(path)]
    for path in (a.output / "composer", a.output / "sdm/include", a.source / "sdm/include",
                 a.source / "sdm/include/core", a.source / "include", a.source / "libdebug",
                 a.references / "android_vendor_qcom_opensource_display-commonsys-intf/gralloc",
                 a.references / "android_vendor_qcom_opensource_display-commonsys-intf/include",
                 ROOT / "probe/graphics"):
        flags += ["-I", str(path)]
    commands, objects, deps = [], [], []
    for name, source in (("allocator", a.output / "composer/hwc_buffer_allocator.cpp"),
                         ("probe", ROOT / "probe/graphics/sm8350-hwc-allocator-probe.cpp")):
        obj = a.output / (name + ".o")
        dep = a.output / (name + ".d")
        command = flags + ["-MD", "-MF", str(dep), "-c", str(source), "-o", str(obj)]
        commands.append(command)
        subprocess.run(command, check=True)
        objects.append(str(obj))
        deps += shlex.split(dep.read_text().split(":", 1)[1].replace("\\\n", " "))
    binary = a.output / "sm8350-hwc-allocator-probe"
    command = flags + objects + ["-L", str(a.platform), "-L", str(a.source_libraries),
        "-L", str(a.donor / "lib64"), "-landroid", "-lEGL", "-lc++", "-lgrallocutils",
        "-lsdmutils", "-ldisplaydebug", "-llog", "-ldl", "-Wl,--no-undefined",
        "-Wl,--allow-shlib-undefined", "-o", str(binary)]
    commands.append(command)
    subprocess.run(command, check=True)
    display_manifest = a.source_libraries / "BUILD.json"
    display_build = json.loads(display_manifest.read_text())
    registry_commands = [c for c in display_build["commands"]
                         if "-c" in c and c[c.index("-c") + 1].endswith("/hw_device_drm.cpp")]
    if len(registry_commands) != 1:
        raise ValueError("Expected one source registry compile command")
    registry = registry_commands[0][:]
    registry[registry.index("-I"):registry.index("-I")] = ["-I", str(a.output / "sdm/include")]
    registry[registry.index("-c") + 1] = str(a.output / "sdm/libs/core/drm/hw_device_drm.cpp")
    registry[registry.index("-o") + 1] = str(a.output / "hw_device_drm.o")
    registry[registry.index("-MF") + 1] = str(a.output / "hw_device_drm.d")
    commands.append(registry)
    subprocess.run(registry, check=True)
    deps += shlex.split((a.output / "hw_device_drm.d").read_text().split(":", 1)[1].replace("\\\n", " "))
    inputs = {str(Path(d).resolve()): sha(Path(d)) for d in deps}
    ndk_libraries = a.cc.parent.parent / "sysroot/usr/lib/aarch64-linux-android/35"
    for path in (a.cc.resolve(), ROOT / "probe/graphics/sm8350-hwc-allocator-pins.json",
                 ROOT / config["patch"], Path(__file__), gralloc,
                 a.source_libraries / "libsdmutils.so", a.source_libraries / "libdisplaydebug.so",
                 a.platform / "libc++.so", ndk_libraries / "libandroid.so", ndk_libraries / "libEGL.so",
                 display_manifest):
        inputs[str(path)] = sha(path)
    build = {"status": "actual-source-HWC-allocator-diagnostic", "deployed": False,
             "hwc_service_built": False, "patch": config, "patch_command": patch_command,
             "commands": commands, "inputs": inputs, "sha256": {binary.name: sha(binary)},
             "limits": ["Runtime not yet verified; use existing mapper5 private namespace",
                        "AllocatedBufferInfo adds an explicit native_handle field; rebuild the matched SDM/HWC set",
                        "HWCLayer and other private-handle consumers still require ABI audit",
                        "No scanout/latency/secure/HDR inference from allocator compilation",
                        "Transitive donor-library relocation closure requires actual runtime load"]}
    (a.output / "BUILD.json").write_text(json.dumps(build, indent=2) + "\n")
    print(json.dumps({key: build[key] for key in ("status", "hwc_service_built", "sha256")}))

if __name__ == "__main__":
    main()
