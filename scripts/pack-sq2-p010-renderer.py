#!/usr/bin/env python3
"""Append the verified renderer pair to a private HIDL boot image."""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path, PurePosixPath
import subprocess

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("pack", ROOT / "scripts/pack-sq2-lts-audio-ssam.py")
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)
sha = lambda b: hashlib.sha256(b).hexdigest()


def bundle(directory, recipe):
    files = {}
    for name, digest in recipe["deployment_sha256"].items():
        if name not in ("libstagefright.so", "libsq2-p010-renderer.so"):
            raise ValueError("Unexpected renderer member")
        data = (directory / name).read_bytes()
        if sha(data) != digest:
            raise ValueError("Unverified renderer: " + name)
        files[name] = data
    if set(files) != {"libstagefright.so", "libsq2-p010-renderer.so"}:
        raise ValueError("Incomplete renderer pair")
    files["BASELINE.sha256"] = (recipe["target_sha256"] + "  libstagefright.so\n").encode()
    files["LIBRARIES.sha256"] = "".join(sha(b) + "  " + n + "\n" for n, b in files.items()
                                           if n.endswith(".so")).encode()
    files["service.rc"] = (ROOT / "configs/sq2-p010-renderer.rc").read_bytes()
    files["SHA256SUMS"] = "".join(sha(b) + "  " + n + "\n" for n, b in files.items()).encode()
    return files


def build(base, files):
    before = pack.cpio.entries(gzip.decompress(base))
    if "bin/quest2-hidl-stage" not in before:
        raise ValueError("Requires existing HIDL integration")
    if any(n.startswith("etc/sq2-p010-renderer") for n in before):
        raise ValueError("Renderer already packaged")
    anchor = b'mount --bind /newroot/debug_ramdisk/keystore2.rc /newroot/system/etc/init/keystore2.rc || fail keystore-debug-overlay\n'
    init = before["init"][1]
    if init.count(anchor) != 1:
        raise ValueError("Unknown init hook")
    helper = (ROOT / "probe/test2/sq2-p010-renderer-stage").read_bytes()
    init = init.replace(anchor, anchor + b"sq2-p010-renderer-stage > /logs/p010-renderer-stage.txt 2>&1 || fail p010-renderer-stage\n")
    for script in (helper, init):
        subprocess.run(["sh", "-n"], input=script, check=True)
    changes = {
        "etc/sq2-p010-renderer": (0o040755, b""),
        "bin/sq2-p010-renderer-stage": (0o100755, helper),
        "init": (before["init"][0], init),
    }
    for name, data in files.items():
        if PurePosixPath(name).name != name:
            raise ValueError("Unexpected bundle path")
        changes["etc/sq2-p010-renderer/" + name] = (0o100644, data)
    raw = b"".join(pack.cpio.entry(n, b, m) for n, (m, b) in changes.items())
    raw += pack.cpio.entry("TRAILER!!!", b"", 0)
    raw += b"\0" * (-len(raw) % 512)
    result = base + gzip.compress(raw, mtime=0) + pack.credential_tail(base)
    if pack.cpio.entries(gzip.decompress(result)) != before | changes:
        raise ValueError("Effective archive changed outside renderer scope")
    return result, changes


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("base", "renderer", "output"):
        p.add_argument(name, type=Path)
    p.add_argument("--expected-base-sha256", required=True)
    a = p.parse_args()
    base = a.base.read_bytes()
    if sha(base) != a.expected_base_sha256:
        raise ValueError("Wrong parent image")
    recipe = json.loads((ROOT / "configs/stagefright-p010-elf.json").read_text())
    files = bundle(a.renderer, recipe)
    result, changes = build(base, files)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    with a.output.open("xb") as out:
        out.write(result)
    report = dict(baseline_sha256=sha(base), sha256=sha(result), bytes=len(result),
                  changed_entries=sorted(changes), libraries=recipe["deployment_sha256"],
                  credentials_kernel_hidl_unchanged=True, private_package=True,
                  deployed=False, reboot_verified=False)
    a.output.with_suffix(".json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
