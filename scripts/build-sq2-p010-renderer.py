#!/usr/bin/env python3
"""Build isolated probes of the actual patched AOSP P010 renderer; never deploy."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("prepared", "toolchains", "omx-dir", "omx-headers", "stagefright", "output"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--with-system-adapter", action="store_true")
    args = parser.parse_args()
    paths = {name: path.resolve() for name, path in vars(args).items() if isinstance(path, Path)}
    source, cache, omx, output = (paths[k] for k in ("prepared", "toolchains", "omx_dir", "output"))
    recipe = json.loads((ROOT / "configs/stagefright-p010-renderer.json").read_text())
    cppdir = source / "media/libstagefright/colorconversion"
    for name, expected in recipe["patched_sources"].items():
        if sha(cppdir / name) != expected:
            parser.error("Patched AOSP source mismatch: " + name)
    output.mkdir(parents=True, exist_ok=False)
    compiler = cache / "sq2-graphics/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang++"
    includes = [
        source / "media/libstagefright/include",
        source / "media/module/foundation/include",
        source / "media/libmedia/include",
        source / "media/libstagefright/omx/include",
        source / "native/libs/ui/include",
        source / "native/libs/nativewindow/include",
        source / "native/libs/nativebase/include",
        source / "native/headers",
        source / "native/headers/media_plugin",
        source / "native/libs/math/include",
        source / "native/include",
        source / "libhardware",
        source / "generated",
        source / "libyuv/include",
        cache / "sq2-c2-core-reference/libutils/include",
        cache / "sq2-c2-core-reference/libcutils/include",
        cache / "sq2-c2-core-reference/libsystem/include",
        cache / "sq2-c2-base-reference/include",
        cache / "sq2-c2-logging-reference/liblog/include",
        cache / "sq2-c2-libhidl-reference/base/include",
        cache / "sq2-c2-libhidl-reference/transport/include",
        cache / "sq2-c2-libhwbinder-reference/include",
        cache / "sq2-c2-native-reference/libs/binder/ndk/include_cpp",
        cache / "sq2-c2-native-reference/libs/binder/ndk/include_ndk",
        omx / "headers/fmq/include", omx / "headers/fmq/base",
        omx / "headers/generated", paths["omx_headers"], output,
    ]
    common = [str(compiler), "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
              "-Wno-unused-parameter", "-Wno-unused-label", "-Wno-unused-variable",
              "-Wno-deprecated-declarations", "-fno-rtti", "-fvisibility=hidden",
              "-ffunction-sections", "-fdata-sections", "-fPIC", "-nostdinc++",
              "-isystem", str(cache / "sq2-c2-platform-libcxx-headers")]
    if args.with_system_adapter:
        common += ["-flto", "-fsanitize=cfi", "-fsanitize-cfi-cross-dso",
                   "-fstack-protector-strong",
                   "-D_FORTIFY_SOURCE=2"]
    for include in includes:
        common += ["-I", str(include)]
    records = {}

    def run(name, command):
        result = subprocess.run([str(x) for x in command], capture_output=True, text=True)
        (output / (name + ".log")).write_text(result.stdout + result.stderr)
        records[name] = {"command": [str(x) for x in command], "exit": result.returncode}
        (output / "BUILD.json").write_text(json.dumps(records, indent=2) + "\n")
        if result.returncode:
            raise RuntimeError("Build failed; see " + str(output / (name + ".log")))

    tracker = source / "media/libstagefright/FrameRenderTracker.cpp"
    text = tracker.read_text()
    # The source uses no Surface API; avoid pulling in all generated GUI AIDL headers.
    text = text.replace("#include <gui/Surface.h>", "#include <unistd.h>")
    (output / "FrameRenderTracker.cpp").write_text(text)
    sources = [cppdir / "SoftwareRenderer.cpp", cppdir / "ColorConverter.cpp",
               output / "FrameRenderTracker.cpp"]
    for src in sources:
        run(src.stem, common + ["-c", src, "-o", output / (src.stem + ".o")])
    renderer = (cppdir / "SoftwareRenderer.cpp").read_text()
    start = renderer.index("static bool validP010Buffer(")
    end = renderer.index("\n}\n", start) + 3
    (output / "sq2-p010-buffer-check.inc").write_text(renderer[start:end])
    libs = ["-nostdlib++", "-L", source / "platform", "-L", omx / "platform",
            "-lstagefright_foundation", "-lutils", "-lc++", "-llog",
            "-Wl,--gc-sections", "-Wl,--allow-shlib-undefined"]
    run("p010-renderer-check", common + [
        ROOT / "tests/fixtures/quest2_video/p010-renderer.cpp", output / "ColorConverter.o",
        *libs, "-o", output / "p010-renderer-check"])
    run("p010-surface-check", common + [
        ROOT / "tests/fixtures/quest2_video/p010-surface.cpp",
        output / "SoftwareRenderer.o", output / "ColorConverter.o",
        output / "FrameRenderTracker.o", source / "libyuv-build/libyuv.a",
        *libs, "-lui", "-lnativewindow", paths["stagefright"], "-landroid", "-lmediandk",
        "-o", output / "p010-surface-check"])
    if args.with_system_adapter:
        helper_source = source / "SurfaceUtils-reference.cpp"
        if sha(helper_source) != recipe["hdr_helper_source_sha256"]:
            raise ValueError("Unexpected SurfaceUtils reference")
        helper = helper_source.read_text()
        start = helper.index("void setNativeWindowHdrMetadata(")
        end = helper.index("\n}\n", start) + 3
        helper_file = output / "HdrMetadata.cpp"
        helper_file.write_text(
            '#include <utils/Log.h>\n#include <media/stagefright/SurfaceUtils.h>\n'
            '#include <system/window.h>\n#include <media/hardware/VideoAPI.h>\nnamespace android {\n' + helper[start:end] + "\n}\n")
        adapter = ROOT / "probe/video/sq2-p010-renderer-adapter.cpp"
        run("libsq2-p010-renderer.so", common + [
            "-Wno-invalid-offsetof", "-Wno-return-type-c-linkage",
            adapter, helper_file, output / "SoftwareRenderer.o", output / "ColorConverter.o",
            source / "libyuv-build/libyuv.a", *libs,
            "-lui", "-lnativewindow", "-lstagefright_codecbase", "-shared",
            "-Wl,-soname,libsq2-p010-renderer.so", "-Wl,-z,defs,-z,relro,-z,now,-z,noexecstack",
            "-o", output / "libsq2-p010-renderer.so"])
        records["libsq2-p010-renderer.so"]["sha256"] = sha(output / "libsq2-p010-renderer.so")
        records["adapter_inputs"] = {str(p): sha(p) for p in (helper_source, helper_file, adapter)}
    for name in ("p010-renderer-check", "p010-surface-check"):
        data = (output / name).read_bytes()
        if data[:6] != b"\x7fELF\x02\x01" or int.from_bytes(data[18:20], "little") != 183:
            raise ValueError("Not ARM64 ELF: " + name)
        records[name]["sha256"] = sha(output / name)
    records["inputs"] = {str(p): sha(p) for p in [
        ROOT / "patches/stagefright-p010-renderer.patch",
        *sources, tracker, source / "libyuv-build/libyuv.a", paths["stagefright"],
        *sorted((source / "platform").glob("*.so")),
        ROOT / "tests/fixtures/quest2_video/p010-renderer.cpp",
        ROOT / "tests/fixtures/quest2_video/p010-surface.cpp",
    ]}
    records["scope"] = "Isolated tests only; system MediaCodec and display tone mapping unverified"
    (output / "BUILD.json").write_text(json.dumps(records, indent=2) + "\n")
    print(json.dumps({k: records[k]["sha256"]
                      for k in ("p010-renderer-check", "p010-surface-check")}, indent=2))


if __name__ == "__main__":
    main()
