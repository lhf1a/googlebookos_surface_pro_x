#!/usr/bin/env python3
"""Build an isolated native QTI gralloc/mapper/EGL candidate; no HAL installation."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
RUNTIME_SUMS_SHA = "395520969a5f1ce766dabde0af258204a17d9dc5c5ea37783f1937590b5c0689"
RUNTIME_LIBS = ("libEGL_adreno.so", "libGLESv2_adreno.so", "libgsl.so", "libadreno_utils.so",
                "eglSubDriverAndroid.so", "libq2v77glnext.so", "libq2v77qgl.so", "qgl_config.txt")
MAPPER_LIBS = ("hw/android.hardware.graphics.mapper@3.0-impl-qti-display.so", "libhidltransport.so")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_runtime(runtime):
    if sha(runtime / "SHA256SUMS") != RUNTIME_SUMS_SHA:
        raise ValueError("Expected the verified 6.18 Direct runtime manifest")
    for line in (runtime / "SHA256SUMS").read_text().splitlines():
        digest, name = line.split()
        if Path(name).name != name or (runtime / name).is_symlink() or sha(runtime / name) != digest:
            raise ValueError("Runtime input mismatch: " + name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vendor-root", type=Path, required=True)
    parser.add_argument("--cc", type=Path, required=True)
    parser.add_argument("--headers", type=Path, required=True)
    parser.add_argument("--hidl", type=Path, required=True, help="Verified sources and generated HIDL headers")
    parser.add_argument("--platform-libs", type=Path, required=True, help="Matching Android platform link libraries")
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--capture-vector", type=Path, help="HEVC Main/NV12 or Main10/P010 fixture for native VPU to GPU probe")
    parser.add_argument("--omx-headers", type=Path, help="Also build OEM native-buffer import probe with pinned OMX headers")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    verify_runtime(args.runtime)
    reference = json.loads((ROOT / "configs/quest2-v77-display-reference.json").read_text())["files"]
    vendor = args.vendor_root / "lib64"
    for name in MAPPER_LIBS:
        if (vendor / name).is_symlink() or sha(vendor / name) != reference["/lib64/" + name]:
            parser.error("Mapper input mismatch: " + name)
    if args.capture_vector:
        vector = json.loads((args.capture_vector / "vector.json").read_text())
        reference_name = "ref.p010" if vector["bit_depth"] == 10 else "ref.nv12"
        if (vector["codec"] != "hevc" or vector["bit_depth"] not in (8, 10) or vector["frames"] != 6 or
                sha(args.capture_vector / "clip.hevc") != vector["stream_sha256"] or
                sha(args.capture_vector / reference_name) != vector["reference_sha256"]):
            parser.error("Expected verified six-frame HEVC NV12/P010 vector")
    if args.omx_headers:
        pins = json.loads((ROOT / "configs/quest2-video-omx-headers.json").read_text())["sha256"]
        for name, digest in pins.items():
            if sha(args.omx_headers / name) != digest:
                parser.error("OMX header mismatch: " + name)
    if args.output.exists():
        parser.error("Output must be fresh")
    commands, inputs = [], {}
    def run(argv):
        commands.append(list(map(str, argv)))
        subprocess.run(commands[-1], check=True)
    def copy(source):
        inputs[str(source)] = sha(source)
        (args.output / source.name).write_bytes(source.read_bytes())
    run([sys.executable, ROOT / "scripts/build-quest2-gralloc-probe.py", "--vendor-root", args.vendor_root,
         "--cc", args.cc, "--headers", args.headers, "--output", args.output])
    allocation_build = json.loads((args.output / "BUILD.json").read_text())
    for name in RUNTIME_LIBS:
        copy(args.runtime / name)
    for name in MAPPER_LIBS:
        copy(vendor / name)
    run(["patchelf", "--replace-needed", "libquest2-ion-dmaheap.so", "libquest2-system-ion.so", args.output / "libgsl.so"])
    include = ["-isystem", args.hidl / "generated", "-isystem", args.hidl / "include"]
    run([args.cc.absolute().with_name("clang++"), "--target=aarch64-linux-android35", "-std=c++17", "-O2", "-fPIC", "-shared",
         "-fno-rtti", "-nostdlib++", "-include", ROOT / "probe/graphics/quest2-platform-cxx.h",
         "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", *include,
         ROOT / "probe/graphics/quest2-native-mapper.cpp", "-L", vendor, "-L", args.platform_libs,
         "-Wl,--no-undefined,-z,global", "-l:vendor.qti.hardware.display.mapper@3.0.so",
         "-lhidlbase", "-lutils", "-lc++", "-llog", "-ldl", "-Wl,-soname,libquest2-native-mapper.so",
         "-o", args.output / "libquest2-native-mapper.so"])
    run(["patchelf", "--add-needed", "libquest2-native-mapper.so", args.output / "eglSubDriverAndroid.so"])
    run([args.cc.resolve(), "--target=aarch64-linux-android35", "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror",
         "-DQUEST2_NATIVE_GPU", "-I", args.headers, ROOT / "probe/graphics/quest2-gralloc-probe.c",
         "-L", args.output, "-lquest2-native-mapper", "-lEGL_adreno", "-lGLESv2_adreno", "-ldl",
         "-o", args.output / "quest2-gralloc-probe"])
    run([args.cc.resolve(), "--target=aarch64-linux-android35", "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror",
         "-I", args.headers, ROOT / "probe/graphics/quest2-yuv-layout-probe.c", "-L", args.output,
         "-lquest2-native-mapper", "-lEGL_adreno", "-lGLESv2_adreno", "-ldl", "-lm", "-o", args.output / "quest2-yuv-layout-probe"])
    if args.capture_vector:
        run([args.cc.resolve(), "--target=aarch64-linux-android35", "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror",
             "-DSQ2_NATIVE_CAPTURE", "-DSQ2_HEVC_VECTOR", "-I", args.capture_vector, "-I", args.headers,
             ROOT / "probe/video/sq2-v4l2-decode-test.c", ROOT / "probe/graphics/quest2-native-video-capture.c",
             "-L", args.output, "-lquest2-native-mapper", "-lEGL_adreno", "-lGLESv2_adreno", "-ldl", "-lm",
             "-o", args.output / "quest2-native-video-probe"])
        for path in [ROOT / "probe/video/sq2-v4l2-decode-test.c", ROOT / "probe/graphics/quest2-native-video-capture.c"] + [args.capture_vector / name for name in ("vector.json", "sq2-hevc-vector.h", "clip.hevc", reference_name)]:
            if path.is_file(): inputs[str(path)] = sha(path)
    if args.omx_headers:
        run([args.cc.resolve(), "--target=aarch64-linux-android35", "-std=gnu11", "-O2",
             "-Wall", "-Wextra", "-Werror", "-DQUEST2_NATIVE_IMPORT", "-I", args.omx_headers,
             "-I", args.headers, ROOT / "probe/video/quest2-omx-probe.c",
             ROOT / "probe/video/quest2-omx-native-import.c", "-ldl", "-o", args.output / "quest2-omx-native-import"])
        run([args.cc.resolve(), "--target=aarch64-linux-android35", "-std=gnu11", "-O2",
             "-Wall", "-Wextra", "-Werror", "-fPIC", "-shared", "-pthread",
             ROOT / "probe/video/quest2-vidc-bridge.c", "-ldl", "-llog", "-o", args.output / "libquest2-vidc-bridge.so"])
        run([args.cc.resolve(), "--target=aarch64-linux-android35", "-std=gnu11", "-O2",
             "-Wall", "-Wextra", "-Werror", "-DQUEST2_NATIVE_OUTPUT", "-I", args.omx_headers,
             "-I", args.headers, ROOT / "probe/video/quest2-omx-decode.c",
             ROOT / "probe/graphics/quest2-native-video-capture.c",
             "-L", args.output, "-lquest2-native-mapper", "-lEGL_adreno", "-lGLESv2_adreno",
             "-ldl", "-lm", "-pthread", "-Wl,--export-dynamic", "-o", args.output / "quest2-omx-native-decode"])
        inputs[str(ROOT / "probe/graphics/quest2-native-video-capture.c")] = sha(ROOT / "probe/graphics/quest2-native-video-capture.c")
        for name in ("quest2-omx-probe.c", "quest2-omx-native-import.c", "quest2-vidc-bridge.c",
                     "quest2-omx-decode.c", "quest2-omx-native-output.h", "quest2-omx-session.h"):
            inputs[str(ROOT / "probe/video" / name)] = sha(ROOT / "probe/video" / name)
        for path in sorted(args.omx_headers.glob("*.h")):
            inputs[str(path)] = sha(path)
    for name in ("quest2-native-mapper.cpp", "quest2-native-mapper-compat.h", "quest2-native-rgb-stride.h", "quest2-native-egl.h", "quest2-gralloc-probe.c", "quest2-platform-cxx.h", "quest2-yuv-layout-probe.c", "quest2-native-egl-context.h", "quest2-native-p010-probe.h"):
        inputs[str(ROOT / "probe/graphics" / name)] = sha(ROOT / "probe/graphics" / name)
    for directory in (args.hidl / "include", args.hidl / "generated", args.platform_libs):
        for path in sorted(directory.rglob("*")):
            if path.is_file(): inputs[str(path)] = sha(path)
    hashes = {f.name: sha(f) for f in sorted(args.output.iterdir()) if f.name not in {"SHA256SUMS", "BUILD.json"}}
    (args.output / "SHA256SUMS").write_text("".join(f"{digest}  {name}\n" for name, digest in hashes.items()))
    (args.output / "BUILD.json").write_text(json.dumps({
        "status": "compiled-not-device-validated", "purpose": __doc__, "allocation_build": allocation_build,
        "inputs": inputs, "commands": commands, "sha256": hashes,
        "limits": ["Private process only; no system allocator replacement", "MSM DRM GPU backend, not KGSL",
                   "Build is not validation; optional YUV/VPU probes need device results; no scanout, UBWC or protected validation"],
    }, indent=2) + "\n")
    print("QUEST2_NATIVE_GPU_CANDIDATE_BUILT", args.output)


if __name__ == "__main__":
    main()
