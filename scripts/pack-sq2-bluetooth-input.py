#!/usr/bin/env python3
"""Build a narrow Bluetooth input addon; never writes disks or changes pairing."""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('cpio', ROOT/'scripts/pack-sq2-vulkan.py')
cpio = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cpio)
sha = lambda b: hashlib.sha256(b).hexdigest()


def build(base, expected):
    if sha(base) != expected:
        raise ValueError('Baseline hash mismatch')
    before = cpio.entries(gzip.decompress(base))
    helper = (ROOT/'probe/test2/bluetooth-framework-boot').read_bytes().replace(b'\r\n', b'\n')
    begin = helper.index(b'# The BLE HID stack disconnects')
    end = helper.index(b'# Do not bring an already-owned controller')
    old = helper[:begin] + helper[end:]
    if before['bin/bluetooth-framework-boot'][1].replace(b'\r\n', b'\n') != old:
        raise ValueError('Unknown baseline Bluetooth helper')
    delta = {'bin/bluetooth-framework-boot': (0o100755, helper),
             'bin/bluetooth-input-watch': (0o100755, (ROOT/'probe/test2/bluetooth-input-watch').read_bytes())}
    for _, body in delta.values():
        subprocess.run(['sh', '-n'], input=body, check=True)
    rest, credentials, last = base, None, None
    while rest:
        dec = zlib.decompressobj(31)
        files = cpio.entries(dec.decompress(rest))
        if not dec.eof:
            raise ValueError('Incomplete gzip member')
        member = rest[:len(rest)-len(dec.unused_data)]
        if 'etc/shadow' in files and 'bin/dropbear' in files:
            credentials = member
        last = member
        rest = dec.unused_data
    if credentials is None or last != credentials:
        raise ValueError('Expected local credential addon last')
    archive = b''.join(cpio.entry(n, body, mode) for n, (mode, body) in delta.items())
    archive += cpio.entry('TRAILER!!!', b'', 0)
    archive += b'\0' * (-len(archive) % 512)
    addon = gzip.compress(archive, mtime=0) + credentials
    after = cpio.entries(gzip.decompress(base+addon))
    changed = {n for n in before.keys() | after.keys() if before.get(n) != after.get(n)}
    if changed != set(delta):
        raise ValueError('Unexpected effective delta')
    return addon, dict(base_sha256=sha(base), addon_sha256=sha(addon),
                       changed=sorted(changed), password_addon_last=True,
                       hardware_boot_tested=False)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('base', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--expected-base-sha256', required=True)
    a = p.parse_args()
    if a.output.exists() or a.output.with_suffix('.audit.json').exists():
        raise ValueError('Refusing existing output/audit')
    addon, audit = build(a.base.read_bytes(), a.expected_base_sha256)
    with a.output.open('xb') as f:
        f.write(addon)
    assert a.output.read_bytes() == addon
    a.output.with_suffix('.audit.json').write_text(json.dumps(audit, indent=2)+'\n')
    print(json.dumps(audit, indent=2))


if __name__ == '__main__':
    main()
