#!/usr/bin/env python3
"""Remove this task's temporary SSH overlay and install a rebuilt SPI-HID.

Preserves current runtime modules and identities; the pre-debug init is used
only to reverse the exact known early-SSH transform, never as a parent payload.
No target block device access, content-hash boot gates, backups or rollback.
"""
import argparse
import gzip
import json
from pathlib import Path
import subprocess

from disk_image import module, require
from full_image import records_from_gzip, new_record
from full_image_home import apply as apply_home


def build(current, normal, spi, output):
    require(not output.exists(), 'Output must be fresh')
    records, bodies = records_from_gzip(current)
    _, clean = records_from_gzip(normal)
    expected = module('normal_ssh_inverse', 'pack-spx-debug-ssh.py').transform(clean)
    require(bodies['init'] == expected['init'], 'Current init has unrelated edits; refusing to replace')
    removed = sorted(set(expected) - {'init'})
    require(all(n not in clean for n in removed), 'Temporary path existed in the normal payload')
    require(all(n in bodies for n in removed), 'Incomplete temporary SSH overlay')
    before = dict(bodies)
    for name in removed:
        del bodies[name]
        del records[name]
    bodies['init'] = clean['init']
    blob = spi.read_bytes()
    require(blob[:4] == b'\x7fELF', 'SPI-HID must be an ELF module')
    vermagic = subprocess.check_output(['modinfo', '-F', 'vermagic', str(spi)], text=True).strip()
    require(vermagic.startswith('6.18.60-sq2-android1 '), 'Wrong SPI-HID kernel ABI')
    names = [n for n in bodies if n.endswith('/spi-hid.ko')]
    require(bool(names), 'No SPI-HID runtime module')
    for name in names:
        bodies[name] = blob
    apply_home(records, bodies, new_record)
    require(b'early_ssh' not in bodies['init'] and b'spx-early-ssh' not in bodies['init'], 'Early SSH hook remains')
    subprocess.run(['sh', '-n'], input=bodies['init'], check=True)
    encode = module('normal_start_encode', 'pack-sq2-release.py').encode
    identity = {'bin/dropbear', 'bin/start-ssh', 'etc/shadow'}
    require(identity <= bodies.keys(), 'Missing original SSH identity')
    def member(names):
        raw = b''.join(encode(n, bodies[n], records[n]) for n in names)
        raw += encode('TRAILER!!!', b'', new_record(0))
        return gzip.compress(raw + b'\0' * (-len(raw) % 512), mtime=0)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('xb') as f:
        f.write(member(sorted(bodies, key=lambda n:(n.count('/'),n))))
        f.write(member(sorted(identity)))
    actual_records, actual = records_from_gzip(output)
    require(actual == bodies, 'Repacked effective records differ')
    require(all(actual_records[n][1][1:6] == records[n][1][1:6] for n in bodies), 'Record metadata differs')
    report = {
        'operation':'latest-touch-home-normal-start', 'current':str(current),
        'output':str(output), 'bytes':output.stat().st_size,
        'removed_records':removed,
        'changed_records':sorted(n for n in bodies if before.get(n) != bodies[n]),
        'spi_module':str(spi), 'vermagic':vermagic,
        'ordinary_ssh_and_credentials_preserved':True,
        'temporary_wifi_ownership_removed':True,
        'sq1_dtb_untouched':True, 'content_hash_gates':False,
        'new_backups':False, 'rollback_tests':False, 'device_written':False,
    }
    output.with_suffix('.json').write_text(json.dumps(report, indent=2)+'\n')
    return report


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('current',type=Path); p.add_argument('normal',type=Path)
    p.add_argument('spi',type=Path); p.add_argument('output',type=Path)
    a=p.parse_args()
    print(json.dumps(build(a.current,a.normal,a.spi,a.output),indent=2),flush=True)
