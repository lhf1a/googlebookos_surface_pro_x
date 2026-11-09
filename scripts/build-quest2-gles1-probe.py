#!/usr/bin/env python3
"""Build a minimal standard EGL/ES1 native clear/scissor readback probe."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
GLES1_SHA = "396987f605b4f7aa1be5d10cacf0216bce400a3c13bb36f9cf29fac1d9bc5ace"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def symbols(path, undefined):
    result = set()
    for line in subprocess.check_output(["readelf", "--dyn-syms", "-W", path], text=True).splitlines():
        fields = line.split()
        if len(fields) >= 8 and (fields[6] == "UND") == undefined:
            result.add(fields[7].split("@")[0])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--cc", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists() or (ROOT / "out").resolve() not in output.parents:
        parser.error("Output must be fresh below repository out/")
    spec = importlib.util.spec_from_file_location("native", ROOT / "scripts/build-quest2-native-gpu-probe.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.verify_runtime(args.runtime)
    reference = json.loads((ROOT / "configs/quest2-native-vulkan-reference.json").read_text())["native_sha256"]
    native_files = [args.native / name for name in ("libgsl.so", "libEGL_adreno.so", "libGLESv2_adreno.so", "eglSubDriverAndroid.so")]
    for path in native_files:
        if path.is_symlink() or sha(path) != reference[path.name]:
            parser.error("Native input mismatch: " + path.name)
    donor = args.runtime / "libGLESv1_CM_adreno.so"
    if donor.is_symlink() or sha(donor) != GLES1_SHA:
        parser.error("Unexpected GLES1 donor")
    imported = symbols(donor, True)
    gsl_imported = imported & symbols(args.runtime / "libgsl.so", False)
    missing = gsl_imported - symbols(args.native / "libgsl.so", False)
    forbidden = {name for name in imported if any(text in name.lower() for text in ("sq2", "quest2", "mapper", "nativebuffer", "hardwarebuffer"))}
    if missing or forbidden or not gsl_imported:
        parser.error("GLES1 native dependency audit failed")
    source = ROOT / "probe/graphics/quest2-gles1-readback.c"
    command = list(map(str, [args.cc, "--target=aarch64-linux-android35", "-std=c11", "-O2",
                             "-Wall", "-Wextra", "-Werror", source, "-lEGL", "-lGLESv1_CM", "-ldl",
                             "-o", output / "quest2-gles1-readback"]))
    output.mkdir(parents=True)
    subprocess.run(command, check=True)
    shutil.copy2(donor, output / donor.name)
    hashes = {path.name: sha(path) for path in sorted(output.iterdir())}
    (output / "SHA256SUMS").write_text("".join(f"{digest}  {name}\n" for name, digest in hashes.items()))
    (output / "BUILD.json").write_text(json.dumps({
        "status": "built-not-device-tested", "command": command, "sha256": hashes,
        "inputs": {str(path): sha(path) for path in [Path(__file__), source, donor, args.runtime / "libgsl.so", args.cc.resolve(), *native_files]},
        "gles1_elf": {"needed": subprocess.check_output(["patchelf", "--print-needed", donor], text=True).splitlines(),
                      "runpath": subprocess.check_output(["patchelf", "--print-rpath", donor], text=True).strip(),
                      "gsl_imports": sorted(gsl_imported), "missing_native_gsl_exports": sorted(missing),
                      "synthetic_or_mapper_imports": sorted(forbidden)},
        "private_runtime": ["Place pinned libGLESv1_CM_adreno.so in the private vendor/lib64/egl directory",
                            "Set libGLESv1_CM_freedreno.so -> libGLESv1_CM_adreno.so in that same private directory",
                            "Existing egl/libgsl.so must resolve to verified mapper5/v33 native GSL; do not copy old GSL"],
        "limits": ["Pbuffer ES1 clear/scissor only; no WSI, AHB import, physical scanout, or performance claim",
                   "Build/static import closure is not rendering validation; run bounded probe in native display environment"]
    }, indent=2) + "\n")
    print("QUEST2_GLES1_PROBE_BUILT", output)


if __name__ == "__main__":
    main()
