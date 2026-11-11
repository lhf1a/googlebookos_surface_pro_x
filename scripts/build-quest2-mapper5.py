#!/usr/bin/env python3
"""Build direct gralloc-backed mapper5 candidate, without registering a system HAL."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
LIBS=("gralloc.kona.so","libgralloccore.so","libgrallocutils.so","libqdMetaData.so",
      "libadreno_utils.so","libgsl.so","libquest2-system-ion.so",
      "libEGL_adreno.so","libGLESv2_adreno.so","eglSubDriverAndroid.so",
      "libq2v77glnext.so","libq2v77qgl.so","qgl_config.txt")

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ("parent","references","aidl-headers","binder-headers","hardware-headers","core-headers","platform","cc","output"):
        parser.add_argument("--"+name,type=Path,required=True)
    for name in ("allocator-headers","common-headers","binder-platform-headers","allocator-platform"):
        parser.add_argument("--"+name,type=Path)
    args=parser.parse_args()
    aidl_inputs=(args.allocator_headers,args.common_headers,args.binder_platform_headers,args.allocator_platform)
    if any(aidl_inputs) and not all(aidl_inputs):parser.error("All four allocator inputs are required together")
    pins=json.loads((ROOT/"configs/quest2-mapper5-reference.json").read_text())["sources"]
    parent=json.loads((args.parent/"BUILD.json").read_text())
    inputs={}
    for entry in pins:
        path=args.references/entry["path"]
        if path.is_symlink() or sha(path)!=entry["sha256"]:
            parser.error("Reference input mismatch: "+str(path))
        inputs[str(path)]=sha(path)
    extension_pins=ROOT/"configs/quest2-qti-mapper-extension-reference.json"
    inputs[str(extension_pins)]=sha(extension_pins)
    for entry in json.loads(extension_pins.read_text())["sources"]:
        path=ROOT/entry["path"]
        if sha(path)!=entry["sha256"]:parser.error("QTI extension ABI header mismatch: "+str(path))
        inputs[str(path)]=sha(path)
    for name in LIBS:
        path=args.parent/name
        if path.is_symlink() or sha(path)!=parent["sha256"][name]:
            parser.error("Native parent library mismatch: "+name)
        inputs[str(path)]=sha(path)
    independent_memory=b"dma-heap-system-uncached-v1\0" in (args.parent/"libquest2-system-ion.so").read_bytes()
    args.output.mkdir(parents=True,exist_ok=False)
    for name in LIBS:
        (args.output/name).write_bytes((args.parent/name).read_bytes())
    common=[str(args.cc), "--target=aarch64-linux-android35","-std=c++20","-O2","-fno-rtti",
            "-nostdlib++","-Wall","-Wextra","-Werror","-Wno-unused-parameter",
            "-include",str(ROOT/"probe/graphics/quest2-platform-cxx.h")]
    for path in (args.references/"include",args.aidl_headers,args.binder_headers,args.core_headers,args.hardware_headers):
        common+=["-isystem",str(path)]
    libs=["-L",str(args.output),"-L",str(args.platform),"-lgrallocutils","-lqdMetaData","-lcutils","-lc++","-llog","-ldl","-Wl,--no-undefined"]
    commands=[
        common+["-fPIC","-shared",str(ROOT/"probe/graphics/quest2-native-mapper5.cpp")]+libs+
            ["-Wl,-soname,mapper.quest2.so","-o",str(args.output/"mapper.quest2.so")]]
    commands.append(common+["-fPIC","-shared",str(ROOT/"probe/graphics/quest2-qti-mapper-extension.cpp")]+libs+
                    ["-Wl,-soname,mapper.qti.so",
                     "-o",str(args.output/"mapper.qti.so")])
    if all(aidl_inputs):
        extra=["-isystem",str(args.allocator_headers),"-isystem",str(args.common_headers),
               "-isystem",str(args.binder_platform_headers),"-L",str(args.allocator_platform),
               "-l:android.hardware.graphics.allocator-V2-ndk.so",
               "-l:android.hardware.common-V2-ndk.so","-lbinder_ndk","-Wno-deprecated-declarations"]
        commands.append(common+[str(ROOT/"probe/graphics/quest2-allocator5-service.cpp")]+libs+extra+
                        ["-o",str(args.output/"quest2-allocator5-service")])
        for directory in aidl_inputs:
            for path in sorted(directory.rglob("*")):
                if path.is_file():inputs[str(path)]=sha(path)
        inputs[str(ROOT/"probe/graphics/quest2-allocator5-service.cpp")]=sha(ROOT/"probe/graphics/quest2-allocator5-service.cpp")
    manifest=ROOT/"configs/quest2-native-mapper5.xml"
    (args.output/"mapper.quest2.xml").write_bytes(manifest.read_bytes())
    inputs[str(manifest)]=sha(manifest)
    for cmd in commands:subprocess.run(cmd,check=True)
    for directory in (args.aidl_headers,args.binder_headers,args.core_headers,args.hardware_headers):
        for path in sorted(directory.rglob("*")):
            if path.is_file():inputs[str(path)]=sha(path)
    for name in ("libcutils.so","libc++.so","liblog.so"):
        inputs[str(args.platform/name)]=sha(args.platform/name)
    for name in ("quest2-native-mapper5.cpp", "quest2-gralloc-backend.h",
                 "quest2-native-mapper-loader.h", "quest2-qti-mapper-extension.cpp", "quest2-yv12-layout.h",
                 "quest2-gralloc-utils-abi.h", "quest2-rgb-layout.h", "quest2-color-metadata.h",
                 "quest2-gralloc-metadata.h", "quest2-platform-cxx.h"):
        inputs[str(ROOT/"probe/graphics"/name)]=sha(ROOT/"probe/graphics"/name)
    hashes={f.name:sha(f) for f in args.output.iterdir()}
    (args.output/"SHA256SUMS").write_text("".join(f"{h}  {n}\n" for n,h in sorted(hashes.items())))
    (args.output/"BUILD.json").write_text(json.dumps({
        "status":"compiled-not-device-validated","inputs":inputs,"commands":commands,"sha256":hashes,
        "parent_build_sha256":sha(args.parent/"BUILD.json"),
        "memory_backend":"dma-heap-system-uncached-v1" if independent_memory else "msm-gem-wc",
        "qti_mapper_extension_companion":{"file":"mapper.qti.so","framework_hal":False,
            "backend":"mapper.quest2.so","single_view_only":True,"metadata_state":"verified shared explicit-set bits"},
        "limits":["RGB linear/UBWC and linear NV12/P010; compressed CPU mapping rejected; full contract incomplete",
                  "No mapper3/4 implementation calls; donor interface libraries still linked",
                  "Optional AIDL allocator requires real Binder validation; YUV, dynamic HDR and system integration incomplete",
                  "Requires matching independent system-uncached DMA heap" if independent_memory else "Uncached memory still requires MSM GEM WC"]
    },indent=2)+"\n")
    print("QUEST2_MAPPER5_BUILT",args.output)

if __name__=="__main__":main()
