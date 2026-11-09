#!/usr/bin/env python3
"""Prepare the original QTI HIDL allocator and an IPC client; never deploy."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def verify_candidate(path):
    build = json.loads((path / "BUILD.json").read_text())
    for name, digest in build["sha256"].items():
        if Path(name).name != name or (path / name).is_symlink() or sha(path / name) != digest:
            raise ValueError("Native candidate mismatch: " + name)
    return build

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("native", "vendor-root", "cc", "hidl", "platform", "allocator-platform", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    for name in ("mapper4-headers", "aidl-headers", "binder-headers", "mapper4-platform"):
        parser.add_argument("--" + name, type=Path)
    args = parser.parse_args()
    mapper4_args = (args.mapper4_headers, args.aidl_headers, args.binder_headers, args.mapper4_platform)
    if any(mapper4_args) and not all(mapper4_args):
        parser.error("Mapper4 import candidate requires all four metadata/header/platform inputs")
    if all(mapper4_args):
        metadata_pin = json.loads((ROOT / "configs/quest2-mapper4-reference.json").read_text())
        if sha(args.mapper4_headers / "gralloctypes/Gralloc4.h") != metadata_pin["gralloc4_header"]["sha256"]:
            parser.error("Mapper4 metadata encoding header mismatch")
    parent = verify_candidate(args.native)
    pins = json.loads((ROOT / "configs/quest2-v77-display-reference.json").read_text())["files"]
    original = {
        "vendor.qti.hardware.display.allocator-service": "/bin/hw/vendor.qti.hardware.display.allocator-service",
        "vendor.qti.hardware.display.allocator@3.0.so": "/lib64/vendor.qti.hardware.display.allocator@3.0.so",
    }
    for name, relative in original.items():
        source = args.vendor_root / relative.lstrip("/")
        if source.is_symlink() or sha(source) != pins[relative]:
            parser.error("Original allocator input mismatch: " + relative)
    args.output.mkdir(parents=True, exist_ok=False)
    inputs = {}
    def copy(source, name=None):
        inputs[str(source)] = sha(source)
        (args.output / (name or source.name)).write_bytes(source.read_bytes())
    for name in parent["sha256"]:
        if name.endswith(".so"):
            copy(args.native / name)
    for name, relative in original.items():
        copy(args.vendor_root / relative.lstrip("/"), name)
    copy(ROOT / "configs/quest2-native-allocator.xml")
    copy(args.native / "android.hardware.graphics.mapper@3.0-impl-qti-display.so", "libquest2-original-mapper3.so")
    copy(args.native / "qgl_config.txt")
    # EGL must use the actual standard mapper HAL, not the earlier private entry.
    egl_patch = ["patchelf", "--remove-needed", "libquest2-native-mapper.so",
                 str(args.output / "eglSubDriverAndroid.so")]
    subprocess.run(egl_patch, check=True)
    command = [
        str(args.cc.absolute().with_name("clang++")), "--target=aarch64-linux-android35",
        "-std=c++17", "-O2", "-fno-rtti", "-nostdlib++", "-Wall", "-Wextra", "-Werror",
        "-Wno-unused-parameter", "-include", str(ROOT / "probe/graphics/quest2-platform-cxx.h"),
        "-isystem", str(args.hidl / "generated"), "-isystem", str(args.hidl / "include"),
        str(ROOT / "probe/graphics/quest2-allocator-probe.cpp"),
        "-L", str(args.output), "-L", str(args.allocator_platform), "-L", str(args.platform),
        "-lquest2-native-mapper", "-l:android.hardware.graphics.allocator@3.0.so",
        "-lhidlbase", "-lutils", "-lc++", "-llog", "-ldl",
        "-o", str(args.output / "quest2-allocator-probe"),
    ]
    subprocess.run(command, check=True)
    commands = [command, egl_patch]
    hal_command = [
        str(args.cc.absolute().with_name("clang++")), "--target=aarch64-linux-android35",
        "-std=c++17", "-O2", "-fPIC", "-shared", "-fno-rtti", "-nostdlib++",
        "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-DQUEST2_MAPPER_HAL",
        "-include", str(ROOT / "probe/graphics/quest2-platform-cxx.h"),
        "-isystem", str(args.hidl / "generated"), "-isystem", str(args.hidl / "include"),
        str(ROOT / "probe/graphics/quest2-native-mapper.cpp"),
        "-L", str(args.vendor_root / "lib64"), "-L", str(args.platform),
        "-Wl,--no-undefined", "-l:vendor.qti.hardware.display.mapper@3.0.so",
        "-lhidlbase", "-lutils", "-lc++", "-llog", "-ldl",
        "-Wl,-soname,android.hardware.graphics.mapper@3.0-impl-quest2.so",
        "-o", str(args.output / "android.hardware.graphics.mapper@3.0-impl-quest2.so"),
    ]
    commands.append(hal_command)
    standard = list(command)
    standard[standard.index("-lquest2-native-mapper")] = "-l:android.hardware.graphics.mapper@3.0.so"
    standard.insert(1, "-DQUEST2_SYSTEM_MAPPER")
    standard[-1] = str(args.output / "quest2-allocator-hal-probe")
    commands.append(standard)
    commands.append([
        str(args.cc), "--target=aarch64-linux-android35", "-std=gnu11", "-O2",
        "-Wall", "-Wextra", "-Werror", str(ROOT / "probe/graphics/quest2-mapper-hal-namespace.c"),
        "-o", str(args.output / "quest2-mapper-hal-namespace"),
    ])
    commands.append([
        str(args.cc), "--target=aarch64-linux-android35", "-std=gnu11", "-O2",
        "-Wall", "-Wextra", "-Werror", str(ROOT / "probe/graphics/quest2-ahardwarebuffer-probe.c"),
        "-landroid", "-o", str(args.output / "quest2-ahardwarebuffer-probe"),
    ])
    commands.append([
        str(args.cc), "--target=aarch64-linux-android35", "-std=gnu11", "-O2",
        "-Wall", "-Wextra", "-Werror", str(ROOT / "probe/graphics/quest2-ahb-egl-probe.c"),
        "-landroid", "-lmediandk", "-lEGL", "-lGLESv3", "-o", str(args.output / "quest2-ahb-egl-probe"),
    ])
    if all(mapper4_args):
        extra = ["-isystem", str(args.mapper4_headers), "-isystem", str(args.aidl_headers),
                 "-isystem", str(args.binder_headers), "-L", str(args.mapper4_platform),
                 "-L", str(args.allocator_platform),
                 "-l:android.hardware.graphics.mapper@4.0.so",
                 "-l:android.hardware.graphics.mapper@3.0.so", "-lgralloctypes"]
        mapper4 = list(hal_command)
        mapper4.remove("-DQUEST2_MAPPER_HAL")
        mapper4[mapper4.index(str(ROOT / "probe/graphics/quest2-native-mapper.cpp"))] = str(ROOT / "probe/graphics/quest2-native-mapper4.cpp")
        mapper4[mapper4.index("-Wl,-soname,android.hardware.graphics.mapper@3.0-impl-quest2.so")] = "-Wl,-soname,android.hardware.graphics.mapper@4.0-impl-quest2.so"
        mapper4[-1] = str(args.output / "android.hardware.graphics.mapper@4.0-impl-quest2.so")
        commands.append(mapper4 + extra)
        client4 = list(standard)
        client4.remove("-DQUEST2_SYSTEM_MAPPER")
        client4[client4.index(str(ROOT / "probe/graphics/quest2-allocator-probe.cpp"))] = str(ROOT / "probe/graphics/quest2-mapper4-probe.cpp")
        client4[-1] = str(args.output / "quest2-mapper4-probe")
        commands.append(client4 + extra)
        for directory in (args.mapper4_headers, args.aidl_headers, args.binder_headers, args.mapper4_platform):
            for path in sorted(directory.rglob("*")):
                if path.is_file(): inputs[str(path)] = sha(path)
        for name in ("quest2-native-mapper4.cpp", "quest2-mapper4-probe.cpp"):
            inputs[str(ROOT / "probe/graphics" / name)] = sha(ROOT / "probe/graphics" / name)
    for step in commands[2:]:
        subprocess.run(step, check=True)
    for path in [ROOT / "probe/graphics/quest2-allocator-probe.cpp",
                 ROOT / "probe/graphics/quest2-platform-cxx.h",
                 args.allocator_platform / "android.hardware.graphics.allocator@3.0.so",
                 args.allocator_platform / "android.hardware.graphics.mapper@3.0.so",
                 ROOT / "probe/graphics/quest2-native-mapper.cpp",
                 ROOT / "probe/graphics/quest2-native-mapper-compat.h",
                 ROOT / "probe/graphics/quest2-native-rgb-stride.h",
                 ROOT / "probe/graphics/quest2-mapper-hal-namespace.c",
                 ROOT / "probe/graphics/quest2-ahardwarebuffer-probe.c",
                 ROOT / "probe/graphics/quest2-ahb-egl-probe.c"]:
        inputs[str(path)] = sha(path)
    for directory in (args.hidl / "include", args.hidl / "generated"):
        for path in sorted(directory.rglob("*")):
            if path.is_file():
                inputs[str(path)] = sha(path)
    hashes = {f.name: sha(f) for f in args.output.iterdir()}
    (args.output / "SHA256SUMS").write_text("".join(f"{digest}  {name}\n" for name, digest in sorted(hashes.items())))
    (args.output / "BUILD.json").write_text(json.dumps({
        "status": "compiled-not-device-validated", "command": command, "commands": commands, "inputs": inputs,
        "native_build_sha256": sha(args.native / "BUILD.json"), "sha256": hashes,
        "mapper4_import_metadata_only": all(mapper4_args),
        "limits": ["No mapper5/AIDL allocator replacement", "Original service with MSM system ION backend",
                   "IPC readback is not GPU/scanout/system Surface validation"],
    }, indent=2) + "\n")
    print("QUEST2_ALLOCATOR_CANDIDATE_BUILT", args.output)

if __name__ == "__main__":
    main()
