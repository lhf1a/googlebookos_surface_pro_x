#!/usr/bin/env python3
"""Replace only the matched msm module in a private initramfs; never deploy."""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("cpio", ROOT / "scripts/pack-sq2-vulkan.py")
cpio = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cpio)

def sha(data):
    return hashlib.sha256(data).hexdigest()

def require(condition, message):
    if not condition:
        raise ValueError(message)

def records(data):
    """Last-entry-wins newc records, including the metadata used by the kernel."""
    pos, result = 0, {}
    while pos < len(data):
        if data[pos] == 0:
            pos += 1
            continue
        require(pos + 110 <= len(data), "Truncated cpio header")
        magic = data[pos:pos + 6]
        require(magic in (b"070701", b"070702"), "Unsupported cpio header")
        fields = tuple(int(data[pos + 6 + i * 8:pos + 14 + i * 8], 16)
                       for i in range(13))
        size, length = fields[6], fields[11]
        require(length > 0 and pos + 110 + length <= len(data), "Invalid cpio name length")
        raw_name = data[pos + 110:pos + 110 + length]
        require(raw_name[-1:] == b"\0" and b"\0" not in raw_name[:-1], "Invalid cpio name")
        name = raw_name[:-1].decode().removeprefix("./")
        begin = (pos + 110 + length + 3) & ~3
        require(begin + size <= len(data), "Truncated cpio body")
        body = data[begin:begin + size]
        if magic == b"070702":
            require(sum(body) & 0xffffffff == fields[12], "Invalid cpio CRC")
        pos = (begin + size + 3) & ~3
        if name != "TRAILER!!!":
            result[name] = (magic, fields, raw_name, body)
    return result

def replacement(record, body):
    magic, old_fields, raw_name, _ = record
    fields = list(old_fields)
    require(len(body) <= 0xffffffff, "Module exceeds newc size limit")
    fields[6] = len(body)
    fields[12] = sum(body) & 0xffffffff if magic == b"070702" else 0
    header = magic + b"".join(f"{value:08x}".encode() for value in fields) + raw_name
    return header + b"\0" * (-len(header) % 4) + body + b"\0" * (-len(body) % 4)

def verify_effective(before, after, name, module):
    require(before.keys() == after.keys(), "Effective initramfs paths changed")
    changed = sorted(n for n in before if before[n] != after[n])
    require(changed == [name], "Unexpected effective initramfs change")
    expected = records(replacement(before[name], module))[name]
    require(after[name] == expected, "Module payload or metadata changed unexpectedly")
    # All other records compare magic, raw name, inode, ownership, modes, links, devices and times.
    return changed

def info(path, field):
    return subprocess.check_output(["modinfo", "-F", field, str(path)], text=True).strip()

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("base", type=Path)
    p.add_argument("module", type=Path)
    p.add_argument("output", type=Path)
    p.add_argument("--expected-base-sha256", required=True)
    p.add_argument("--expected-module-sha256", required=True)
    a = p.parse_args()
    require(not a.output.exists(), "Choose a fresh private output directory")
    base, module = a.base.read_bytes(), a.module.read_bytes()
    require(sha(base) == a.expected_base_sha256, "Base hash mismatch")
    require(sha(module) == a.expected_module_sha256, "Module hash mismatch")
    require(module[:6] == b"\x7fELF\x02\x01" and int.from_bytes(module[18:20], "little") == 183,
            "Expected little-endian ELF64 ARM64 module")
    before = records(gzip.decompress(base))
    names = [n for n in before if n.startswith("lib/modules/") and
             n.endswith("/kernel/drivers/gpu/drm/msm/msm.ko")]
    require(len(names) == 1, "Expected one existing msm module")
    name = names[0]
    require(before[name][1][1] & 0o170000 == 0o100000, "Expected regular msm module")
    require(before[name][1][4] == 1, "Hard-linked msm module requires a separate packaging strategy")
    with tempfile.TemporaryDirectory(prefix="qti-dpu-pack-") as temporary:
        old = Path(temporary) / "msm.ko"
        old.write_bytes(before[name][3])
        # Existing module indexes remain correct only if the external module contract is unchanged.
        for field in ("name", "vermagic", "depends", "alias", "softdep"):
            require(sorted(info(old, field).splitlines()) == sorted(info(a.module, field).splitlines()),
                    "Module index/ABI contract changed: " + field)
        kernel_version = name.split("/")[2]
        require(info(a.module, "vermagic").split()[0] == kernel_version, "Kernel version mismatch")
        def exports(path):
            lines = subprocess.check_output(["aarch64-linux-gnu-nm", "--defined-only", str(path)],
                                             text=True).splitlines()
            return sorted(l.split()[-1] for l in lines if "__ksymtab_" in l)
        require(exports(old) == exports(a.module), "Export index changed")
    credential_member = None
    rest = base
    while rest:
        dec = zlib.decompressobj(31)
        payload = dec.decompress(rest)
        require(dec.eof, "Truncated gzip member")
        consumed = len(rest) - len(dec.unused_data)
        files = records(payload)
        if "etc/shadow" in files and "bin/dropbear" in files:
            credential_member = rest[:consumed]
        rest = dec.unused_data
    archive = replacement(before[name], module) + cpio.entry("TRAILER!!!", b"", 0)
    archive += b"\0" * (-len(archive) % 512)
    delta = gzip.compress(archive, mtime=0) + (credential_member or b"")
    output = base + delta
    after = records(gzip.decompress(output))
    changed = verify_effective(before, after, name, module)
    a.output.mkdir(parents=True, exist_ok=False)
    (a.output / "display.delta").write_bytes(delta)
    (a.output / "initramfs.cpio.gz").write_bytes(output)
    result = {"base_sha256": sha(base), "output_sha256": sha(output),
              "delta_sha256": sha(delta), "module_sha256": sha(module), "changed": changed,
              "credential_member_preserved_last": bool(credential_member),
              "all_effective_metadata_verified": True,
              "module_metadata_preserved_except": ["filesize", "checksum"],
              "private_artifact": True, "deployed": False, "reboot_verified": False}
    (a.output / "BUILD.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))

if __name__ == "__main__":
    main()
