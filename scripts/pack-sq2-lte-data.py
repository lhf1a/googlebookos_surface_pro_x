#!/usr/bin/env python3
"""Build-only private LTE bundle. Preserve every unrelated effective cpio entry."""
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
PINNED = {
    'ipa_sq2_lab.ko': '9b6e6dd6659f5344205927d0e27c66c564786955a414bef6cc73957e8c049abd',
    'rmnet.ko': '0aaaf85f42b38bbb87cc9434790998313fc6a7e2130baa47cf97dd58927da315',
    'firmware/qcom/sc8180x/sq2/ipa_fws.elf': '510f10022640b99042a05aac6ead42435deb4d4e509ec8f4a78b851b4f911b18',
}
RUNTIME = [*PINNED, 'sq2-qmi-data', 'sq2-qmi-mode', 'sq2-rmnet-create', 'sq2-lte.jar']
sha = lambda b: hashlib.sha256(b).hexdigest()
spec = importlib.util.spec_from_file_location('cpio', ROOT/'scripts/pack-sq2-vulkan.py')
cpio = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cpio)


def build(base, bundle, serial, fsg_hash, apn):
    if sha(base) != BASE_SHA:
        raise ValueError('Verified complete Android baseline required')
    if not re.fullmatch(r'[A-Za-z0-9-]{8,48}', serial):
        raise ValueError('Explicit unit serial required')
    if not re.fullmatch(r'[A-Za-z0-9.-]{1,100}', apn):
        raise ValueError('Invalid APN')
    data = {n: (bundle/n).read_bytes() for n in RUNTIME}
    for name, expected in PINNED.items():
        if sha(data[name]) != expected:
            raise ValueError('Untested runtime: '+name)
    fsg = (bundle/'modem_fsg.bin').read_bytes()
    if len(fsg) != 2097152 or sha(fsg) != fsg_hash or not any(fsg):
        raise ValueError('Own-unit FSG identity mismatch')
    before = cpio.entries(gzip.decompress(base))
    if 'etc/sq2-lte' in before:
        raise ValueError('Already contains LTE bundle')
    mode, wifi = before['bin/wifi-services']
    anchor = b'done\nqrtr-ns -f '
    if wifi.count(anchor) != 1:
        raise ValueError('Unexpected RMTFS seed site')
    mode_init, init = before['init']
    boot_anchor = b'    lockscreen-framework-boot > /logs/lockscreen-framework-boot.txt 2>&1 &\n'
    if init.count(boot_anchor) != 1:
        raise ValueError('Unexpected Android startup site')
    delta = {
        'bin/wifi-services': (mode, wifi.replace(anchor, b'done\nlte-unit-seed || echo "LTE_UNIT_SEED_FAILED"\nqrtr-ns -f ')),
        'init': (mode_init, init.replace(boot_anchor, boot_anchor+b'    lte-framework-boot > /logs/lte-framework-boot.txt 2>&1 &\n')),
        'etc/sq2-lte': (0o40700, b''),
        'etc/sq2-lte/modem_fsg.bin': (0o100600, fsg),
        'etc/sq2-lte/FSG.sha256': (0o100600, (fsg_hash+'  modem_fsg.bin\n').encode()),
        'etc/sq2-lte/unit-serial': (0o100600, (serial+'\n').encode()),
        'etc/sq2-lte/apn': (0o100600, (apn+'\n').encode()),
    }
    for name in ['lte-unit-seed', 'lte-framework-boot']:
        delta['bin/'+name] = (0o100755, (ROOT/'probe/lte'/name).read_bytes().replace(b'\r\n', b'\n'))
    for name, body in data.items():
        path = Path('etc/sq2-lte')/name
        for parent in reversed(path.parents):
            key = parent.as_posix()
            if key != '.' and key not in before:
                delta.setdefault(key, (0o40700, b''))
        delta[path.as_posix()] = (0o100700 if name.startswith('sq2-') and not name.endswith('.jar') else 0o100600, body)
    delta['etc/sq2-lte/runtime.sha256'] = (0o100600, ''.join(sha(data[n])+'  '+n+'\n' for n in RUNTIME).encode())
    rest = base
    while rest:
        dec = zlib.decompressobj(31)
        files = cpio.entries(dec.decompress(rest))
        if not dec.eof:
            raise ValueError('Truncated gzip member')
        credential = rest[:len(rest)-len(dec.unused_data)]
        rest = dec.unused_data
    if 'etc/shadow' not in files or 'bin/dropbear' not in files:
        raise ValueError('Private password SSH member must already be last')
    archive = b''.join(cpio.entry(n, body, mode) for n, (mode, body) in delta.items())
    archive += cpio.entry('TRAILER!!!', b'', 0)
    archive += b'\0' * (-len(archive) % 512)
    output = base+gzip.compress(archive, mtime=0)+credential
    after = cpio.entries(gzip.decompress(output))
    changed = {n for n in before.keys() | after.keys() if before.get(n) != after.get(n)}
    if changed != set(delta) or any(before[n] != after[n] for n in before.keys()-delta.keys()):
        raise ValueError('Unexpected effective file change')
    if sha(after['etc/sq2-audio/audio-hal'][1]) != '774cd7e731bd5a5fcda6edbde6e97464bd0e04acc1603c3be7b0dbe7a69561ea':
        raise ValueError('Accepted audio HAL missing')
    return output, dict(base_sha256=sha(base), output_sha256=sha(output), bytes=len(output),
                        changed=sorted(changed), runtime={n:sha(b) for n,b in data.items()},
                        all_other_effective_entries_identical=True, credential_member_last=True,
                        private_unit_fsg=True, deployed=False)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ['base','bundle','output']:
        p.add_argument(name,type=Path)
    p.add_argument('--unit-serial',required=True)
    p.add_argument('--fsg-sha256',required=True)
    p.add_argument('--apn',required=True)
    a = p.parse_args()
    if a.output.exists() or a.output.with_suffix('.audit.json').exists():
        p.error('Use fresh output paths')
    blob, audit = build(a.base.read_bytes(),a.bundle,a.unit_serial,a.fsg_sha256,a.apn)
    with a.output.open('xb') as f:
        f.write(blob)
    a.output.with_suffix('.audit.json').write_text(json.dumps(audit,indent=2)+'\n')
    print(json.dumps(audit,indent=2))


if __name__ == '__main__':
    main()
