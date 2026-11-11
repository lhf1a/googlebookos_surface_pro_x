#!/usr/bin/env python3
"""Build a private donor gralloc memory candidate; never register/install a HAL."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
LIBRARIES = {
    "hw/gralloc.kona.so", "libgralloccore.so", "libgrallocutils.so",
    "libqdMetaData.so", "libadreno_utils.so", "libgsl.so",
}

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vendor-root", type=Path, required=True)
    parser.add_argument("--cc", type=Path, required=True)
    parser.add_argument("--headers", type=Path, required=True, help="Android hardware header root")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    reference = json.loads((ROOT / "configs/quest2-v77-display-reference.json").read_text())
    reference["files"].update({"/lib64/" + name: digest for name, digest in
        json.loads((ROOT / "configs/quest2-gles-v77-inputs.json").read_text())["files"].items()})
    for name in sorted(LIBRARIES):
        path = args.vendor_root / "lib64" / name
        if path.is_symlink() or sha(path) != reference["files"]["/lib64/" + name]:
            parser.error("Donor input mismatch: " + name)
    if args.output.exists():
        parser.error("Output must be fresh")
    args.output.mkdir(parents=True)
    commands = []
    def run(argv):
        commands.append([str(x) for x in argv])
        subprocess.run(commands[-1], check=True)
    common = [args.cc.resolve(), "--target=aarch64-linux-android35", "-std=gnu11",
              "-O2", "-Wall", "-Wextra", "-Werror"]
    run(common + ["-fPIC", "-shared", ROOT / "probe/graphics/quest2-system-ion.c",
                  "-Wl,-soname,libquest2-system-ion.so", "-o", args.output / "libquest2-system-ion.so"])
    run(common + ["-I", args.headers, ROOT / "probe/graphics/quest2-gralloc-probe.c",
                  "-ldl", "-o", args.output / "quest2-gralloc-probe"])
    for name in sorted(LIBRARIES):
        source = args.vendor_root / "lib64" / name
        target = args.output / Path(name).name
        target.write_bytes(source.read_bytes())
        if name in {"libgralloccore.so", "libgsl.so"}:
            run(["patchelf", "--replace-needed", "libion.so", "libquest2-system-ion.so", target])
    products = sorted(p for p in args.output.iterdir() if p.is_file())
    hashes = {p.name: sha(p) for p in products}
    (args.output / "SHA256SUMS").write_text("".join(f"{digest}  {name}\n" for name, digest in hashes.items()))
    report = {
        "status": "compiled-not-device-validated",
        "purpose": "private gralloc1 allocation probe; no global HAL registration",
        "donor_build": reference["build"],
        "memory_backend": "dma-heap-system-uncached-v1",
        "inputs": {name: reference["files"]["/lib64/" + name] for name in sorted(LIBRARIES)},
        "sha256": hashes, "commands": commands,
        "source_sha256": {name: sha(ROOT / "probe/graphics" / name) for name in (
            "quest2-system-ion.c", "quest2-system-ion.h", "quest2-gralloc-probe.c")},
        "limits": ["nonsecure system heap only", "requires matching independent system-uncached DMA heap",
                   "no KGSL, VPU, scanout, UBWC or protected-path validation"],
    }
    (args.output / "BUILD.json").write_text(json.dumps(report, indent=2) + "\n")
    print("QUEST2_GRALLOC_CANDIDATE_BUILT")

if __name__ == "__main__":
    main()
