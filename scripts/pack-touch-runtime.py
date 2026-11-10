#!/usr/bin/env python3
"""Offline touch-daemon-only update; preserve parent members and private tail.

No device access, profile import, EFI selection, deployment or reboot occurs.
The caller supplies explicit hashes for both the parent and the tested daemon.
"""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import zlib

ROOT = Path(__file__).resolve().parents[1]


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'scripts' / (name + '.py'))
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


def sha(data):
    return hashlib.sha256(data).hexdigest()


def final_member_offset(data):
    """Locate the last member without retaining large decompressed payloads."""
    offset = 0
    last = 0
    while offset < len(data):
        last = offset
        decoder = zlib.decompressobj(31)
        while not decoder.eof:
            chunk = data[offset:offset + 1024 * 1024]
            if not chunk:
                raise ValueError('Truncated gzip member')
            decoder.decompress(chunk)
            offset += len(chunk) - len(decoder.unused_data)
    return last


def build(base, daemon, output, expected_parent, expected_daemon):
    base, daemon, output = map(Path, (base, daemon, output))
    if output.exists() or output.resolve() in (base.resolve(), daemon.resolve()):
        raise ValueError('Fresh output required')
    raw, binary = base.read_bytes(), daemon.read_bytes()
    if sha(raw) != expected_parent or sha(binary) != expected_daemon:
        raise ValueError('Input SHA256 mismatch')
    if binary[:6] != b'\x7fELF\x02\x01' or int.from_bytes(binary[18:20], 'little') != 183:
        raise ValueError('Expected AArch64 ELF64 daemon')
    pack = module('pack-sq2-qti-system-display')
    release = module('pack-sq2-release')
    before, _ = pack.scan(base)
    target = 'bin/iptsd'
    if target not in before or before[target][1][4] != 1 or before[target][1][1] & 0o170000 != 0o100000:
        raise ValueError('Missing, nonregular or hardlinked daemon')
    if before[target][2] == sha(binary):
        raise ValueError('Daemon already current')
    offset = final_member_offset(raw)
    tail = module('pack-sq2-vulkan').entries(gzip.decompress(raw[offset:]))
    if not {'etc/shadow', 'bin/dropbear'} <= tail.keys() or target in tail:
        raise ValueError('Expected final identity member without touch override')
    trailer = ('070701', [0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 11, 0], '')
    delta = release.encode(target, binary, before[target]) + release.encode('TRAILER!!!', b'', trailer)
    delta += b'\0' * (-len(delta) % 512)
    result = raw[:offset] + gzip.compress(delta, mtime=0) + raw[offset:]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(result)
    after, bodies = pack.scan(output, [target])
    if set(after) != set(before) or bodies[target] != binary:
        raise ValueError('Effective entry mismatch')
    for name in before:
        if name != target and before[name] != after[name]:
            raise ValueError('Unrelated content or metadata changed: ' + name)
    a, b = list(before[target][1]), list(after[target][1])
    for index in (6, 12):
        a[index] = b[index] = 0
    if a != b:
        raise ValueError('Daemon ownership or mode changed')
    report = dict(parent_sha256=sha(raw), sha256=sha(result), daemon_sha256=sha(binary),
                  changed=[target], unchanged_records=len(before)-1,
                  original_compressed_members_preserved=True,
                  final_private_identity_member_preserved=True,
                  per_device_profiles_embedded=False, device_written=False)
    output.with_suffix('.audit.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('base', 'daemon', 'output'):
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--expected-parent', required=True)
    p.add_argument('--expected-daemon', required=True)
    a = p.parse_args()
    print(json.dumps(build(a.base, a.daemon, a.output, a.expected_parent, a.expected_daemon), indent=2))
