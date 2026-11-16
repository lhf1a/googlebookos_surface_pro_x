#!/usr/bin/env python3
"""Build camera modules, frozen AIDL, HAL and QTI buffer wrappers from source.

Only explicit build dependencies are read. No Surface connection, Windows driver,
previous camera binary, initramfs, calibration or credentials are used.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys

from full_image_camera import ROOT, SENSORS, CAMSS, write_inputs
from kernel_baseline import validate_kernel_source, validate_build_tree

PATH_KEYS = ("kernel_source", "kernel_build", "toolchains", "interfaces",
             "aidl_compiler", "dependencies", "libraries", "platform_headers",
             "binder_library", "layout_headers", "hidl_headers", "allocator_headers")


def read_inputs(path):
    data = json.loads(path.read_text())
    for key in PATH_KEYS:
        data[key] = (path.parent / data[key]).resolve()
    data["graphics_library_dirs"] = [(path.parent / p).resolve()
                                     for p in data["graphics_library_dirs"]]
    data.setdefault("cross_compile", "aarch64-linux-gnu-")
    return data


def graphics_commands(inputs):
    tc = inputs["toolchains"]
    ndk = tc/"sq2-graphics/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64"
    source = ROOT/"probe/graphics"
    common = [str(ndk/"bin/clang++"), "--target=aarch64-linux-android35", "-std=c++20",
              "-O2", "-fno-rtti", "-nostdlib++", "-Wall", "-Wextra", "-Werror",
              "-Wno-unused-parameter", "-include", str(source/"quest2-platform-cxx.h")]
    for path in (inputs["platform_headers"], inputs["layout_headers"],
                 tc/"sq2-c2-native-reference/libs/binder/ndk/include_cpp",
                 inputs["hidl_headers"], tc/"sq2-graphics/src/mesa-26.2.4/include/android_stub"):
        common += ["-isystem", str(path)]
    libraries = []
    for path in inputs["graphics_library_dirs"]:
        libraries += ["-L", str(path)]
    libraries += ["-lgrallocutils", "-lqdMetaData", "-lcutils", "-lc++",
                  "-llog", "-ldl", "-Wl,--no-undefined"]
    mapper = common + ["-fPIC", "-shared", str(source/"quest2-native-mapper5.cpp")]
    mapper += libraries + ["-Wl,-soname,mapper.quest2.so", "-o", "mapper.quest2.so"]
    allocator = common + [str(source/"quest2-allocator5-service.cpp")] + libraries
    for path in (inputs["allocator_headers"],
                 tc/"sq2-c2-aidl-generated/include",
                 tc/"sq2-c2-native-reference/libs/binder/ndk/include_platform"):
        allocator += ["-isystem", str(path)]
    allocator += ["-l:android.hardware.graphics.allocator-V2-ndk.so",
                  "-l:android.hardware.common-V2-ndk.so", "-lbinder_ndk",
                  "-Wno-deprecated-declarations", "-o", "quest2-allocator5-service"]
    return [mapper, allocator]


def build(inputs, output):
    validate_kernel_source(inputs["kernel_source"])
    validate_build_tree(inputs["kernel_build"])
    output.mkdir(parents=True, exist_ok=False)
    work = output/"build"
    work.mkdir()
    commands = []
    def run(label, command):
        command = list(map(str, command))
        print("Building "+label, flush=True)
        with (work/(label+".log")).open("w") as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
        commands.append({"step": label, "command": command, "exit": result.returncode})
        (work/"commands.json").write_text(json.dumps(commands, indent=2)+"\n")
        if result.returncode:
            raise RuntimeError("Build failed: "+str(work/(label+".log")))
    for label, script in (("sensors", "build-spx-camera-probe.sh"), ("camss", "build-spx-camss.sh")):
        run(label, ["env", "CROSS_COMPILE="+inputs["cross_compile"], "sh", ROOT/"scripts"/script,
                    inputs["kernel_source"], inputs["kernel_build"], work/label])
    tc = inputs["toolchains"]
    ndk = tc/"sq2-graphics/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64"
    run("aidl", [sys.executable, ROOT/"scripts/build-spx-camera-aidl.py",
                 inputs["interfaces"], inputs["aidl_compiler"], ndk, work/"aidl",
                 "--binder-headers", tc/"sq2-c2-native-reference/libs/binder/ndk"])
    run("provider", [sys.executable, ROOT/"scripts/build-spx-camera-provider.py",
                     inputs["dependencies"], work/"aidl", inputs["libraries"], tc, work/"provider",
                     "--platform-headers", inputs["platform_headers"],
                     "--binder-library", inputs["binder_library"]])
    recipe = work/"graphics-commands.json"
    recipe.write_text(json.dumps({"commands": graphics_commands(inputs)}, indent=2)+"\n")
    run("graphics", [sys.executable, ROOT/"scripts/build-spx-camera-graphics.py",
                     recipe, ROOT/"probe/graphics", work/"graphics"])
    files = {name+".ko": (work/directory/(name+".ko")).read_bytes()
             for directory, names in (("sensors", SENSORS), ("camss", CAMSS)) for name in names}
    files["provider"] = (work/"provider").read_bytes()
    for name in ("mapper.quest2.so", "quest2-allocator5-service"):
        files[name] = (work/"graphics"/name).read_bytes()
    write_inputs(files, output/"camera")
    print("Camera inputs: "+str(output/"camera"), flush=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--inputs", required=True, type=Path, help="source/SDK paths; relative to this JSON")
    p.add_argument("--output", required=True, type=Path, help="fresh build directory")
    args = p.parse_args()
    build(read_inputs(args.inputs.resolve()), args.output.resolve())


if __name__ == "__main__":
    main()
