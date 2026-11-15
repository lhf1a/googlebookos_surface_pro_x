#!/usr/bin/env python3
"""Create an experimental Hybrid camera-BLOB initramfs; never installs it."""
import argparse
import gzip
import hashlib
import importlib.util
from pathlib import Path
import zlib

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('base', type=Path)
parser.add_argument('graphics', type=Path, help='matching mapper.quest2.so and quest2-allocator5-service')
parser.add_argument('output', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('cpio', root/'scripts/pack-sq2-vulkan.py')
cpio = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cpio)
base = args.base.read_bytes()
before = cpio.entries(gzip.decompress(base))
prefix = 'etc/sq2-qti-system/'
changes = {}
for relative, binary in (
    ('lib64/hw/mapper.quest2.so', 'mapper.quest2.so'),
    ('lib64/sq2-qti-display/mapper.quest2.so', 'mapper.quest2.so'),
    ('bin/hw/vendor.qti.hardware.display.allocator-service', 'quest2-allocator5-service'),
):
    name = prefix+'root/'+relative
    mode, old = before[name]
    data = (args.graphics/binary).read_bytes()
    assert data[:6] == b'\x7fELF\x02\x01' and int.from_bytes(data[18:20], 'little') == 183
    assert old[:6] == data[:6]
    assert relative in before[prefix+'INSTALL_FILES'][1].decode().splitlines()
    changes[name] = (mode, data)
# Preserve both checksum inventories used by this display payload.
for name in (prefix+'SHA256SUMS', prefix+'root/SHA256SUMS'):
    if name not in before:
        continue
    mode, data = before[name]
    lines = []
    for line in data.decode().splitlines():
        digest, relative = line.split(None, 1)
        target = str(Path(name).parent/relative)
        if target in changes:
            assert hashlib.sha256(before[target][1]).hexdigest() == digest
            digest = hashlib.sha256(changes[target][1]).hexdigest()
        lines.append(digest+'  '+relative)
    changes[name] = (mode, ('\n'.join(lines)+'\n').encode())
# The private identity archive must remain last, exactly as in the camera module packer.
rest = base
credential = None
while rest:
    decoder = zlib.decompressobj(31)
    entries = cpio.entries(decoder.decompress(rest))
    assert decoder.eof
    member = rest[:len(rest)-len(decoder.unused_data)]
    if 'etc/shadow' in entries and 'bin/dropbear' in entries:
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
assert after['init'] == before['init']
with args.output.open('xb') as stream:
    stream.write(result)
print('changed', len(changes), 'entries')
print('sha256', hashlib.sha256(result).hexdigest())
