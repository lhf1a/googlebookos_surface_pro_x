#!/usr/bin/env python3
"""Build-only, explicitly authorized SQ1 whole-FS1/FS2 RAM startup trial."""
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
DONOR = {'modem_fs1': '62528314a0a0e1cedc97b0b4da04ebedb8923ac395f1ffdaf64548b01b911f10',
         'modem_fs2': '1247bc6f9bfcc63ae3270a25e6fe1994ea2076f22f21d2002a38bb61bbcabe27'}
sha = lambda data: hashlib.sha256(data).hexdigest()
spec = importlib.util.spec_from_file_location('cpio', ROOT/'scripts/pack-sq2-vulkan.py')
cpio = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cpio)


def build(base, fs1, fs2, serial):
    if sha(base) != BASE_SHA:
        raise ValueError('Current verified complete Android baseline required')
    pair = dict(zip(DONOR, [fs1, fs2]))
    for index, (name, data) in enumerate(pair.items(), 1):
        if len(data) != 2097152 or sha(data) != DONOR[name] or data[40:47] != f'IMGEFS{index}'.encode():
            raise ValueError('Explicit donor identity mismatch: '+name)
    if not re.fullmatch(r'[A-Za-z0-9-]{8,48}', serial):
        raise ValueError('Expected explicit target unit serial')
    before = cpio.entries(gzip.decompress(base))
    mode, helper = before['bin/wifi-services']
    anchor = b'done\nqrtr-ns -f '
    if helper.count(anchor) != 1 or b'el2-rmtfs-start > /logs/rmtfs.txt' not in helper:
        raise ValueError('Unexpected effective RMTFS startup site')
    if 'etc/sq2-lte' in before or 'bin/lte-fs12-seed' in before:
        raise ValueError('Baseline already includes a candidate')
    insertion = b'done\n# Explicit once-only SQ1 FS1/FS2 RAM startup comparison.\nlte-fs12-seed || echo "LTE_FS12_SKIPPED: preserving baseline startup"\nqrtr-ns -f '
    delta = {
        'bin/wifi-services': (mode, helper.replace(anchor, insertion)),
        'bin/lte-fs12-seed': (0o100755, (ROOT/'probe/test2/lte-fs12-seed').read_bytes().replace(b'\r\n', b'\n')),
        'etc/sq2-lte': (0o40700, b''),
        **{'etc/sq2-lte/'+name+'.bin': (0o100600, data) for name, data in pair.items()},
        'etc/sq2-lte/FS12.sha256': (0o100600, ''.join(h+'  '+n+'.bin\n' for n,h in DONOR.items()).encode()),
        'etc/sq2-lte/unit-serial': (0o100600, (serial+'\n').encode()),
    }
    rest = base
    while rest:
        dec = zlib.decompressobj(31)
        files = cpio.entries(dec.decompress(rest))
        if not dec.eof:
            raise ValueError('Truncated gzip member')
        credential = rest[:len(rest)-len(dec.unused_data)]
        rest = dec.unused_data
    if 'etc/shadow' not in files or 'bin/dropbear' not in files:
        raise ValueError('Private credential member must already be last')
    archive = b''.join(cpio.entry(name, body, mode) for name, (mode,body) in delta.items())
    archive += cpio.entry('TRAILER!!!', b'', 0)
    archive += b'\0' * (-len(archive) % 512)
    output = base + gzip.compress(archive, mtime=0) + credential
    after = cpio.entries(gzip.decompress(output))
    changed = {n for n in before.keys() | after.keys() if before.get(n) != after.get(n)}
    if changed != set(delta):
        raise ValueError('Unexpected effective entry change')
    for name in before.keys()-delta.keys():
        if before[name] != after[name]:
            raise ValueError('Preserved entry changed: '+name)
    if sha(after['etc/sq2-audio/audio-hal'][1]) != '774cd7e731bd5a5fcda6edbde6e97464bd0e04acc1603c3be7b0dbe7a69561ea':
        raise ValueError('Accepted audio HAL missing')
    return output, dict(base_sha256=sha(base), output_sha256=sha(output), donor_sha256=DONOR,
                        changed=sorted(changed), all_other_effective_entries_identical=True,
                        credential_member_last=True, activation='fs12-seed.ARM consumed durably before adoption',
                        scope='whole opaque SQ1 FS1/FS2, not isolated calibration; FSG stays original',
                        deployed=False, lte_working=False)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('base', type=Path)
    p.add_argument('fs1', type=Path)
    p.add_argument('fs2', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--unit-serial', required=True)
    a = p.parse_args()
    if a.output.exists() or a.output.with_suffix('.audit.json').exists():
        p.error('Use fresh output paths')
    output, audit = build(a.base.read_bytes(), a.fs1.read_bytes(), a.fs2.read_bytes(), a.unit_serial)
    with a.output.open('xb') as f:
        f.write(output)
    a.output.with_suffix('.audit.json').write_text(json.dumps(audit, indent=2)+'\n')
    print(json.dumps(audit, indent=2))


if __name__ == '__main__':
    main()
