#!/usr/bin/env python3
"""Build-only own-unit FSG lab candidate; keep all other effective bytes and credentials."""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import zlib

ROOT = Path(__file__).resolve().parents[1]
BASE_SHA = '957b5ee68d7a0d845638f7ebec9c0f624e0ae4184418bc8a12381628c720e452'
spec = importlib.util.spec_from_file_location('cpio', ROOT/'scripts/pack-sq2-vulkan.py')
cpio = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cpio)
sha = lambda data: hashlib.sha256(data).hexdigest()


def build(base, fsg, expected_fsg, serial):
    if sha(base) != BASE_SHA or len(fsg) != 2097152 or sha(fsg) != expected_fsg:
        raise ValueError('Baseline/FSG identity mismatch')
    if not re.fullmatch(r'[A-Za-z0-9-]{8,48}', serial):
        raise ValueError('Expected explicit unit serial, not a path')
    if not any(fsg) or fsg == b'\xff' * len(fsg):
        raise ValueError('Blank FSG refused')
    before = cpio.entries(gzip.decompress(base))
    mode, helper = before['bin/wifi-services']
    anchor = b'done\nqrtr-ns -f '
    if helper.count(anchor) != 1 or b'el2-rmtfs-start > /logs/rmtfs.txt' not in helper:
        raise ValueError('Unexpected effective Wi-Fi/RMTFS helper')
    if sha(before['etc/sq2-audio/audio-hal'][1]) != '774cd7e731bd5a5fcda6edbde6e97464bd0e04acc1603c3be7b0dbe7a69561ea':
        raise ValueError('Actual accepted audio HAL missing')
    if 'etc/sq2-lte' in before or 'bin/lte-fsg-seed' in before:
        raise ValueError('Already contains a candidate')
    insertion = b'done\n# Explicit own-unit LTE lab request; absent marker is a silent no-op.\nlte-fsg-seed || echo "LTE_FSG_SKIPPED: preserving baseline startup"\nqrtr-ns -f '
    delta = {
        'bin/wifi-services': (mode, helper.replace(anchor, insertion)),
        'bin/lte-fsg-seed': (0o100755, (ROOT/'probe/test2/lte-fsg-seed').read_bytes().replace(b'\r\n', b'\n')),
        'etc/sq2-lte': (0o40700, b''),
        'etc/sq2-lte/modem_fsg.bin': (0o100600, fsg),
        'etc/sq2-lte/FSG.sha256': (0o100600, (expected_fsg+'  modem_fsg.bin\n').encode()),
        'etc/sq2-lte/unit-serial': (0o100600, (serial+'\n').encode()),
    }
    rest = base
    members = []
    while rest:
        dec = zlib.decompressobj(31)
        files = cpio.entries(dec.decompress(rest))
        if not dec.eof:
            raise ValueError('Truncated gzip member')
        member = rest[:len(rest)-len(dec.unused_data)]
        members.append((files, member))
        rest = dec.unused_data
    files, credential = members[-1]
    if 'etc/shadow' not in files or 'bin/dropbear' not in files:
        raise ValueError('Private credential member must already be last')
    archive = b''.join(cpio.entry(name, body, mode) for name, (mode, body) in delta.items())
    archive += cpio.entry('TRAILER!!!', b'', 0)
    archive += b'\0' * (-len(archive) % 512)
    output = base + gzip.compress(archive, mtime=0) + credential
    after = cpio.entries(gzip.decompress(output))
    changed = {name for name in before.keys() | after.keys() if before.get(name) != after.get(name)}
    if changed != set(delta):
        raise ValueError('Unexpected effective entry delta')
    for name in ['init', 'bin/el2-rmtfs-start', 'bin/usb-input-watch', 'bin/usb-network-watch', 'etc/shadow', 'bin/dropbear']:
        if before[name] != after[name]:
            raise ValueError('Preserved entry changed: '+name)
    return output, dict(base_sha256=sha(base), output_sha256=sha(output), fsg_sha256=sha(fsg),
                        changed=sorted(changed), credential_member_last=True,
                        activation='explicit fsg-seed.ARM consumed before RMTFS; otherwise silent no-op',
                        format='opaque own-unit type-GUID reference; content validity not established',
                        deployed=False, lte_working=False)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('base', type=Path)
    p.add_argument('fsg', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--expected-fsg-sha256', required=True)
    p.add_argument('--unit-serial', required=True)
    a = p.parse_args()
    if a.output.exists() or a.output.with_suffix('.audit.json').exists():
        p.error('Use a fresh output')
    output, audit = build(a.base.read_bytes(), a.fsg.read_bytes(), a.expected_fsg_sha256, a.unit_serial)
    with a.output.open('xb') as f:
        f.write(output)
    a.output.with_suffix('.audit.json').write_text(json.dumps(audit, indent=2)+'\n')
    print(json.dumps(audit, indent=2))


if __name__ == '__main__':
    main()
