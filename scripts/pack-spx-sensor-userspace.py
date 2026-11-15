#!/usr/bin/env python3
"""Add the independent sensor sub-HAL and RPC file service before Android init."""
import argparse
import gzip
import hashlib
import importlib.util
from pathlib import Path
import zlib
import zipfile
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('base', type=Path)
parser.add_argument('hal', type=Path)
parser.add_argument('rpc', type=Path)
parser.add_argument('config', type=Path)
parser.add_argument('calibration', type=Path)
parser.add_argument('overlay', type=Path)
parser.add_argument('output', type=Path)
a = parser.parse_args()
root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('cpio', root/'scripts/pack-sq2-vulkan.py')
cpio = importlib.util.module_from_spec(spec); spec.loader.exec_module(cpio)
base = a.base.read_bytes()
before = cpio.entries(gzip.decompress(base))
mode, init = before['init']
marker = b'qti-system-display-stage > /logs/qti-system-display.txt 2>&1 || fail qti-system-display\n'
assert init.count(marker) == 1 and b'spx-sensors-stage' not in init
assert 'bin/spx-sensors-boot' in before
changes = {
    'init': (mode, init.replace(marker, marker+b'spx-sensors-stage > /logs/sensors-stage.txt 2>&1 || echo SPX_SENSORS_STAGE_FAILED\n')),
    'bin/spx-sensors-stage': (0o100755, (root/'probe/sensors/spx-sensors-stage').read_bytes()),
    'etc/spx-sensors/userspace': (0o40755, b''),
}
for src,name,mode in (
    (a.hal,'etc/spx-sensors/userspace/sensors.spx.so',0o100644),
    (a.rpc,'bin/spx-sensor-rpcd',0o100755)):
    data=src.read_bytes()
    assert data[:6] == b'\x7fELF\x02\x01' and int.from_bytes(data[18:20], 'little') == 183
    changes[name]=(mode,data)
with zipfile.ZipFile(a.overlay) as apk:
    assert apk.testzip() is None and {'AndroidManifest.xml','resources.arsc'} <= set(apk.namelist())
changes['etc/spx-sensors/userspace/spx-sensors-overlay.apk']=(0o100644,a.overlay.read_bytes())
cal=a.calibration.read_bytes()
assert len(cal)==1480 and (zlib.crc32(cal[:1476])^0xffffffff)==int.from_bytes(cal[1476:],'little')
changes['etc/spx-sensors/userspace/spx-tcs3430-golden.bin']=(0o100644,cal)
for directory in ('rpc-root','rpc-root/sensors','rpc-root/sensors/config','rpc-root/sensors/registry'):
    changes['etc/spx-sensors/'+directory]=(0o40755,b'')
files=list(a.config.glob('*.json'))
assert files
for src in files+[a.config/'config_list.txt']:
    changes['etc/spx-sensors/rpc-root/sensors/config/'+src.name]=(0o100644,src.read_bytes())
rest = base
credential = None
while rest:
    decoder = zlib.decompressobj(31)
    files = cpio.entries(decoder.decompress(rest))
    assert decoder.eof
    member = rest[:len(rest)-len(decoder.unused_data)]
    if 'etc/shadow' in files and 'bin/dropbear' in files:
        credential = member
    rest = decoder.unused_data
assert credential
archive = b''.join(cpio.entry(n, data, mode) for n, (mode, data) in changes.items())
archive += cpio.entry('TRAILER!!!', b'', 0)
archive += b'\0' * (-len(archive) % 512)
result = base + gzip.compress(archive, mtime=0) + credential
after = cpio.entries(gzip.decompress(result))
assert {n for n in before.keys() | after.keys() if before.get(n) != after.get(n)} == set(changes)
assert after['etc/shadow'] == before['etc/shadow']
with a.output.open('xb') as out:
    out.write(result)
print('changed', len(changes), 'entries')
print('sha256', hashlib.sha256(result).hexdigest())
