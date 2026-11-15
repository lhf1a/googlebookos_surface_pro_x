#!/usr/bin/env python3
"""Add the tested camera modules to an existing private initramfs, preserving all other entries."""
import argparse
import gzip
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import zlib

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('cpio', root/'scripts/pack-sq2-vulkan.py')
cpio = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cpio)
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('base', type=Path)
parser.add_argument('sensor_modules', type=Path)
parser.add_argument('camss_modules', type=Path)
parser.add_argument('output', type=Path)
args = parser.parse_args()
base = args.base.read_bytes()
before = cpio.entries(gzip.decompress(base))
mode, init = before['init']
marker = b'if [ "$rescue" = 1 ]; then\n'
assert init.count(marker) == 1 and b'spx-camera-boot' not in init
hook = b'''if [ "$smoke" = 0 ] && [ "$ssd_smoke" = 0 ] && [ "$rescue" = 0 ]; then
    /bin/spx-camera-boot > /logs/camera-boot.txt 2>&1 || echo 'SPX_CAMERA_NOT_READY: see RAM log'
fi
'''
delta = {'init': (mode, init.replace(marker, hook+marker)),
         'etc/spx-camera': (0o40755, b''), 'etc/spx-camera/modules': (0o40755, b''),
         'bin/spx-camera-boot': (0o100755, (root/'probe/camera/spx-camera-boot').read_bytes())}
vermagic = set()
for directory, names in ((args.sensor_modules, 'qcom_spmi-regulator camcc-sc8180x i2c-qcom-cci v4l2-async v4l2-fwnode v4l2-cci ov5693 ov13858'),
                         (args.camss_modules, 'videobuf2-dma-sg qcom-camss')):
    for name in names.split():
        module = directory/(name+'.ko')
        data = module.read_bytes()
        assert data[:6] == b'\x7fELF\x02\x01' and int.from_bytes(data[18:20], 'little') == 183
        vermagic.add(subprocess.check_output(['modinfo', '-F', 'vermagic', str(module)], text=True).strip())
        delta['etc/spx-camera/modules/'+module.name] = (0o100644, data)
assert len(vermagic) == 1
release = next(iter(vermagic)).split()[0]
assert any(n.startswith('lib/modules/'+release+'/') for n in before), 'module ABI differs from base'
# Existing local credentials must remain byte-identical and last in the archive chain.
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
archive = b''.join(cpio.entry(name, data, mode) for name, (mode, data) in delta.items())
archive += cpio.entry('TRAILER!!!', b'', 0)
archive += b'\0' * (-len(archive) % 512)
result = base + gzip.compress(archive, mtime=0) + credential
after = cpio.entries(gzip.decompress(result))
assert {n for n in before.keys() | after.keys() if before.get(n) != after.get(n)} == set(delta)
assert after['etc/shadow'] == before['etc/shadow']
assert after['init'] == delta['init']
with args.output.open('xb') as stream:
    stream.write(result)
print('kernel', release)
print('changed', len(delta), 'entries')
print('sha256', hashlib.sha256(result).hexdigest())
