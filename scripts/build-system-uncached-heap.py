#!/usr/bin/env python3
"""Build a matched ARM64 WC heap module and both Android ION consumers; no deployment."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def prepare(source, output):
    pins = json.loads((ROOT / "configs/system-uncached-heap.json").read_text())
    patch = ROOT / "patches/system-uncached-heap.patch"
    if sha(source) != pins["source_sha256"] or sha(patch) != pins["patch_sha256"]:
        raise ValueError("Unrecognized exporter source or patch")
    output.mkdir(parents=True, exist_ok=False)
    (output / "system_heap.c").write_bytes(source.read_bytes())
    subprocess.run(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(patch)], cwd=output, check=True)
    if sha(output / "system_heap.c") != pins["patched_sha256"]:
        raise ValueError("Unexpected patched exporter")
    (output / "Makefile").write_text("obj-m += system_uncached_heap.o\nsystem_uncached_heap-y := system_heap.o\n")
    return pins

def build(kernel_build, cc, output):
    kernel_build, cc, output = kernel_build.resolve(), cc.resolve(), output.absolute()
    if output.exists():
        raise ValueError("Use a fresh output directory")
    source = (kernel_build / "source").resolve()
    config = (kernel_build / ".config").read_text()
    if "CONFIG_ARM64=y\n" not in config or "CONFIG_DMABUF_HEAPS=y\n" not in config:
        raise ValueError("Requires ARM64 and DMA heaps")
    release = (kernel_build / "include/config/kernel.release").read_text().strip()
    if not release.startswith("6.18."):
        raise ValueError("Requires the matching 6.18 build")
    if not (kernel_build / "Module.symvers").stat().st_size:
        raise ValueError("Missing strict MODPOST symbol table")
    pins = prepare(source / "drivers/dma-buf/heaps/system_heap.c", output / "module")
    inputs = {str(p): sha(p) for p in [kernel_build / ".config", kernel_build / "Module.symvers", cc,
              ROOT / "probe/graphics/quest2-system-ion.h", ROOT / "probe/graphics/quest2-system-ion.c",
              ROOT / "probe/video/quest2-vidc-bridge.c", ROOT / "patches/system-uncached-heap.patch"]}
    commands = [["make", "-C", str(kernel_build), "M=" + str(output / "module"), "ARCH=arm64",
                 "CROSS_COMPILE=aarch64-linux-gnu-", "KBUILD_MODPOST_WARN=", "-j4", "modules"]]
    common = [str(cc), "--target=aarch64-linux-android35", "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror", "-fPIC", "-shared"]
    commands += [common + [str(ROOT / "probe/graphics/quest2-system-ion.c"),
                 "-Wl,-soname,libquest2-system-ion.so", "-o", str(output / "libquest2-system-ion.so")],
                 common + ["-pthread", "-DQUEST2_NATIVE_EXTRA_DEFAULT=1", str(ROOT / "probe/video/quest2-vidc-bridge.c"),
                 "-llog", "-ldl", "-o", str(output / "libquest2-vidc-bridge.so")]]
    # Record transitive project headers as well as the two translation units.
    for stem, src in [("ion", "probe/graphics/quest2-system-ion.c"), ("bridge", "probe/video/quest2-vidc-bridge.c")]:
        dep = subprocess.check_output([arg for arg in common if arg != "-shared"] + ["-MM", str(ROOT / src)], text=True)
        (output / (stem + ".d")).write_text(dep)
        for item in dep.replace("\\\n", " ").split(":", 1)[1].split():
            p = Path(item)
            if p.is_file(): inputs[str(p)] = sha(p)
    (output / "COMMANDS.json").write_text(json.dumps(commands, indent=2) + "\n")
    with (output / "build.log").open("w") as log:
        for cmd in commands:
            subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, check=True)
    products = ["module/system_uncached_heap.ko", "libquest2-system-ion.so", "libquest2-vidc-bridge.so"]
    hashes = {name: sha(output / name) for name in products}
    report = dict(status="built-not-device-validated", kernel_release=release, exporter=pins,
                  inputs=inputs, commands=commands, sha256=hashes, backend="dma-heap-system-uncached-v1")
    (output / "BUILD.json").write_text(json.dumps(report, indent=2) + "\n")
    (output / "SHA256SUMS").write_text("".join(f"{h}  {n}\n" for n, h in hashes.items()))
    return report

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("kernel-build", "cc", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    build(args.kernel_build, args.cc, args.output)
    print("SYSTEM_UNCACHED_BUNDLE_BUILT", args.output)
