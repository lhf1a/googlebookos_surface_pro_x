#!/usr/bin/env python3
"""Fix the capability of an already prefix-converted private lab policy.

Only clears genfs_seclabel_wildcard; preserves all rule/permissive/credential
bytes. Requires the exact input hash. Does not deploy or change live policy.
"""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('cpio', ROOT/'scripts/pack-sq2-vulkan.py')
cpio = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cpio)

def fix_policy(data):
    result = bytearray(data)
    def u32(offset):
        return struct.unpack_from('<I', data, offset)[0]
    assert u32(0) == 0xf97cff8c and u32(4) == 8 and data[8:16] == b'SE Linux'
    version = u32(16)
    assert 22 <= version <= 33, version
    # Kernel binary policy header followed by policycaps ebitmap.
    offset = 32
    mapsize, highbit, count = struct.unpack_from('<III', data, offset)
    assert mapsize == 64 and count <= highbit // 64
    changed = []
    for n in range(count):
        node = offset + 12 + n * 12
        start, bits = struct.unpack_from('<IQ', data, node)
        if start <= 11 < start + 64:
            mask = 1 << (11-start)
            assert bits & mask, 'Input is not wildcard-enabled'
            struct.pack_into('<Q', result, node+4, bits & ~mask)
            changed.append(node+4)
    assert len(changed) == 1
    assert sum(a != b for a, b in zip(data, result)) == 1
    return bytes(result)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('input', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--expected-sha256', required=True)
    a = p.parse_args()
    sha = lambda b: hashlib.sha256(b).hexdigest()
    before = a.input.read_bytes()
    assert sha(before) == a.expected_sha256
    entries = cpio.entries(gzip.decompress(before))
    name = 'etc/android-native-test-policy'
    mode, policy = entries[name]
    # This is specifically the prefix-converted policy, not an arbitrary
    # native-wildcard policy. Full semantic validation is part of packaging.
    assert b'/net_shared*' not in policy and b'/net_shared' in policy
    modified = fix_policy(policy)
    raw = cpio.entry(name, modified, mode) + cpio.entry('TRAILER!!!', b'', 0)
    candidate = before + gzip.compress(raw, mtime=0)
    expected = dict(entries)
    expected[name] = (mode, modified)
    assert cpio.entries(gzip.decompress(candidate)) == expected
    with a.output.open('xb') as f:
        f.write(candidate)
    assert a.output.read_bytes() == candidate and a.input.read_bytes() == before
    report = dict(baseline_sha256=sha(before), sha256=sha(candidate),
                  policy_before=sha(policy), policy_after=sha(modified),
                  changed_entry=name, policy_bytes_changed=1,
                  changed_field='policycaps.genfs_seclabel_wildcard: 1 -> 0',
                  bytes=len(candidate), hardware_verified=False)
    a.output.with_suffix('.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))

if __name__ == '__main__':
    main()
