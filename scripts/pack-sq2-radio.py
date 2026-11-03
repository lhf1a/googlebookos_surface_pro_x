#!/usr/bin/env python3
"""Append a Radio candidate to verified LTE03; preserve private SSH member last."""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import zlib

ROOT = Path(__file__).resolve().parents[1]
BASE = '783cdacf105c3a4c6f2cf57774fc507dbcd2dc6823f0a32403b6ca3d2d4dca52'
sha = lambda blob: hashlib.sha256(blob).hexdigest()
spec = importlib.util.spec_from_file_location('cpio', ROOT / 'scripts/pack-sq2-vulkan.py')
cpio = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cpio)


def build(base, bundle):
    if sha(base) != BASE:
        raise ValueError('Verified LTE03 base required')
    before = cpio.entries(gzip.decompress(base))
    if 'etc/sq2-radio' in before:
        raise ValueError('Already contains Radio candidate')
    mode, init = before['init']
    anchor = b'apply-device-info > /logs/device-info.txt 2>&1 || fail device-info\n'
    if init.count(anchor) != 1:
        raise ValueError('Unexpected early stage site')
    delta = {
        'init': (mode, init.replace(anchor, b'radio-stage > /logs/radio-stage.txt 2>&1 || fail radio-stage\n' + anchor)),
        'bin/radio-stage': (0o100755, (ROOT / 'probe/lte/radio-stage').read_bytes().replace(b'\r\n', b'\n')),
        'bin/lte-framework-boot': (0o100755, (ROOT / 'probe/lte/lte-framework-boot').read_bytes().replace(b'\r\n', b'\n')),
        'etc/sq2-radio': (0o40700, b''),
    }
    data = {n: (bundle / n).read_bytes() for n in ('sq2-radio', 'sq2-radio-overlay.apk')}
    for name in ('sq2-qmi-data', 'sq2-qmi-mode'):
        data[name] = before['etc/sq2-lte/' + name][1]
    for name in ('sq2-radio.rc', 'sq2-radio-features.xml', 'sq2-radio-manifest.xml'):
        data[name] = (ROOT / 'configs' / name).read_bytes().replace(b'\r\n', b'\n')
    for name, contents in data.items():
        delta['etc/sq2-radio/' + name] = (0o100600, contents)
    delta['etc/sq2-radio/SHA256SUMS'] = (0o100600, ''.join(sha(b) + '  ' + n + '\n' for n, b in data.items()).encode())
    rest = base
    while rest:
        decoder = zlib.decompressobj(31)
        files = cpio.entries(decoder.decompress(rest))
        if not decoder.eof:
            raise ValueError('Truncated gzip member')
        credentials = rest[:len(rest)-len(decoder.unused_data)]
        rest = decoder.unused_data
    if 'etc/shadow' not in files or 'bin/dropbear' not in files:
        raise ValueError('Expected private SSH addon last')
    archive = b''.join(cpio.entry(name, data, mode) for name, (mode, data) in delta.items())
    archive += cpio.entry('TRAILER!!!', b'', 0)
    archive += b'\0' * (-len(archive) % 512)
    output = base + gzip.compress(archive, mtime=0) + credentials
    after = cpio.entries(gzip.decompress(output))
    changed = {n for n in before.keys() | after.keys() if before.get(n) != after.get(n)}
    if changed != set(delta) or any(before[n] != after[n] for n in before.keys() - delta.keys()):
        raise ValueError('Unrelated effective file changed')
    return output, dict(base_sha256=BASE, output_sha256=sha(output), bytes=len(output),
                        changed=sorted(changed), credentials_last=True,
                        runtime={n: sha(b) for n, b in data.items()}, boot_tested=False)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('base', 'bundle', 'output'):
        p.add_argument(name, type=Path)
    a = p.parse_args()
    if a.output.exists():
        p.error('Fresh output path required')
    output, audit = build(a.base.read_bytes(), a.bundle)
    with a.output.open('xb') as f:
        f.write(output)
    a.output.with_suffix('.audit.json').write_text(json.dumps(audit, indent=2) + '\n')
    print(json.dumps(audit, indent=2))
