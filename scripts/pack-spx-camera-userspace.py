#!/usr/bin/env python3
"""Add the experimental camera provider and media profiles before Android init."""
import argparse
import gzip
import hashlib
import importlib.util
from pathlib import Path
import zlib
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('base', type=Path)
parser.add_argument('provider', type=Path)
parser.add_argument('output', type=Path)
a = parser.parse_args()
root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('cpio', root/'scripts/pack-sq2-vulkan.py')
cpio = importlib.util.module_from_spec(spec); spec.loader.exec_module(cpio)
base = a.base.read_bytes()
before = cpio.entries(gzip.decompress(base))
mode, init = before['init']
marker = b'qti-system-display-stage > /logs/qti-system-display.txt 2>&1 || fail qti-system-display\n'
assert init.count(marker) == 1 and b'spx-camera-stage' not in init
assert 'bin/spx-camera-boot' in before
provider = a.provider.read_bytes()
assert provider[:6] == b'\x7fELF\x02\x01' and int.from_bytes(provider[18:20], 'little') == 183
changes = {
    'init': (mode, init.replace(marker, marker+b'spx-camera-stage > /logs/camera-stage.txt 2>&1 || echo SPX_CAMERA_STAGE_FAILED\n')),
    'bin/spx-camera-stage': (0o100755, (root/'probe/camera/spx-camera-stage').read_bytes()),
    'etc/spx-camera/userspace': (0o40755, b''),
    'etc/spx-camera/userspace/provider': (0o100755, provider),
    'etc/spx-camera/userspace/media_profiles_spx.xml': (0o100644, (root/'probe/camera/media_profiles_spx.xml').read_bytes()),
}
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
