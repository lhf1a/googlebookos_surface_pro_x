#!/usr/bin/env python3
"""Pair the independent WC exporter and both consumers in the actual private parent."""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("display_pack", ROOT / "scripts/pack-sq2-qti-system-display.py")
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)
ION = "etc/sq2-qti-system/root/lib64/libquest2-system-ion.so"
DISPLAY_ION = "etc/sq2-qti-system/root/lib64/sq2-qti-display/libquest2-system-ion.so"
BRIDGE = "etc/sq2-hidl/lib64/libquest2-vidc-bridge.so"
MODULE = "etc/sq2-memory/system_uncached_heap.ko"
MARKER = b"dma-heap-system-uncached-v1\0"


def update_sums(data, prefix, before, delta):
    lines = []
    for line in data.decode().splitlines(keepends=True):
        fields = line.split()
        pack.require(len(fields) == 2 and len(fields[0]) == 64, "Malformed checksum record")
        name = prefix + fields[1].removeprefix("./")
        if name in delta:
            pack.require(fields[0] == before[name][2], "Stale parent checksum: " + name)
            line = pack.digest(delta[name]) + line[64:]
        lines.append(line)
    return "".join(lines).encode()


def build(base, bundle, expected, output):
    pack.require(not output.exists(), "Use a fresh output directory")
    pack.require(pack.file_hash(base) == expected, "Actual parent hash mismatch")
    manifest = json.loads((bundle / "BUILD.json").read_text())
    products = {}
    for name in ("module/system_uncached_heap.ko", "libquest2-system-ion.so", "libquest2-vidc-bridge.so"):
        data = (bundle / name).read_bytes()
        pack.require(pack.digest(data) == manifest["sha256"][name], "Bundle mismatch: " + name)
        pack.require(data[:6] == b"\x7fELF\x02\x01" and int.from_bytes(data[18:20], "little") == 183, "Expected ARM64 ELF")
        products[name] = data
    pack.validate_memory_pair(products["libquest2-system-ion.so"], products["libquest2-vidc-bridge.so"], products["module/system_uncached_heap.ko"])
    pack.require(MARKER in products["libquest2-system-ion.so"], "Requires independent memory bundle")
    sums = {"etc/sq2-hidl/SHA256SUMS": "etc/sq2-hidl/",
            "etc/sq2-qti-system/SHA256SUMS": "etc/sq2-qti-system/",
            "etc/sq2-qti-system/INSTALLED_SHA256SUMS": "etc/sq2-qti-system/root/"}
    names = {"init", ION, DISPLAY_ION, BRIDGE, "etc/sq2-hidl/quest2-omx-service", *sums}
    before, selected = pack.scan(base, names)
    pack.require(names <= selected.keys(), "Parent lacks the current QTI/OMX stack")
    pack.require(MODULE not in before, "Parent already contains an independent heap; review its migration")
    release = pack.load_module("pack-sq2-release")
    release.validate_native_pair(selected["etc/sq2-hidl/quest2-omx-service"], bundle / "libquest2-vidc-bridge.so")
    init = selected["init"]
    pack.require(init.count(pack.HOOK) == 1, "Ambiguous HIDL load hook")
    load = b"(cd /etc/sq2-memory; sha256sum -c SHA256SUMS) || fail memory-integrity\ninsmod /etc/sq2-memory/system_uncached_heap.ko || fail memory-heap\n"
    delta = {ION: products["libquest2-system-ion.so"], DISPLAY_ION: products["libquest2-system-ion.so"],
             BRIDGE: products["libquest2-vidc-bridge.so"], "init": init.replace(pack.HOOK, load + pack.HOOK)}
    subprocess.run(["sh", "-n"], input=delta["init"], check=True)
    for name, prefix in sums.items():
        delta[name] = update_sums(selected[name], prefix, before, delta)
    cpio = pack.load_module("pack-sq2-vulkan")
    archive = b"".join(release.encode(n, data, before[n]) for n, data in delta.items())
    extra = {"etc/sq2-memory": (0o40755, b""), MODULE: (0o100644, products["module/system_uncached_heap.ko"]),
             "etc/sq2-memory/SHA256SUMS": (0o100644, (pack.digest(products["module/system_uncached_heap.ko"]) + "  system_uncached_heap.ko\n").encode())}
    archive += b"".join(cpio.entry(n, data, mode) for n, (mode, data) in extra.items())
    archive += cpio.entry("TRAILER!!!", b"", 0)
    archive += b"\0" * (-len(archive) % 512)
    output.mkdir(parents=True)
    image = output / "initramfs.cpio.gz"
    with image.open("xb") as stream, base.open("rb") as original:
        shutil.copyfileobj(original, stream)
        stream.write(gzip.compress(archive, mtime=0))
        stream.write(pack.credential_tail(base.read_bytes()))
    delta.update({n: data for n, (_, data) in extra.items()})
    after, actual = pack.scan(image, delta)
    changed = {n for n in before.keys() | after.keys() if before.get(n) != after.get(n)}
    pack.require(changed == set(delta), "Unrelated effective records changed")
    pack.require(actual == delta, "Effective memory payload mismatch")
    pack.require(pack.file_hash(base) == expected, "Parent changed during packing")
    report = dict(status="candidate-not-boot-validated", base_sha256=expected, bundle_manifest_sha256=pack.file_hash(bundle / "BUILD.json"),
                  sha256=pack.file_hash(image), changed=sorted(changed), kernel_release=manifest["kernel_release"],
                  limits=["Private parent credentials retained", "No GPU or display provider replacement", "No default pin promotion"])
    (output / "BUILD.json").write_text(json.dumps(report, indent=2) + "\n")
    return report

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("base", "bundle", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--expected-base-sha256", required=True)
    args = parser.parse_args()
    print(json.dumps(build(args.base, args.bundle, args.expected_base_sha256, args.output), indent=2))
