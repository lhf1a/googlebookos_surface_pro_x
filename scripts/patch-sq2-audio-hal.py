#!/usr/bin/env python3
"""Pinned Cuttlefish 16373615 HAL workaround for physical SQ2 ALSA.

Return after the real StreamAlsaMonoPipe transfer succeeds, bypassing
StreamPrimary's emulator-only sleep/skip loop. Preserve error and stub paths.
Source reference: platform/hardware/interfaces audio/aidl/default/primary/StreamPrimary.cpp
This is an experimental binary workaround, not a generic HAL patch.
"""
import hashlib
import json
import pathlib
import struct
import sys

src, dst = map(pathlib.Path, sys.argv[1:3])
data = bytearray(src.read_bytes())
expected = 'fd219b1058eccc701de685cf59d6578e887c11182d877bb4e6c3a78bfd73e279'
assert hashlib.sha256(data).hexdigest() == expected, 'Unrecognized HAL build'
assert data[:6] == b'\x7fELF\x02\x01'
phoff = struct.unpack_from('<Q', data, 32)[0]
entsize, count = struct.unpack_from('<HH', data, 54)
va, target = 0xd1bd4, 0xd1e34
offset = None
for i in range(count):
    typ, flags, off, addr, _, size, _, _ = struct.unpack_from('<IIQQQQQQ', data, phoff+i*entsize)
    if typ == 1 and flags & 1 and addr <= va and va+4 <= addr+size:
        offset = off+va-addr
assert offset is not None
before = struct.unpack_from('<I', data, offset)[0]
assert before == 0x340001a0, hex(before)  # cbz w0, original emulator pacing block
after = 0x34000000 | (((target-va)//4 & 0x7ffff) << 5)  # cbz w0, common epilogue
struct.pack_into('<I', data, offset, after)
dst.write_bytes(data)
record = dict(original_sha256=expected, patched_sha256=hashlib.sha256(data).hexdigest(),
              address=hex(va), file_offset=hex(offset), original=hex(before), patched=hex(after),
              purpose='Return successful physical ALSA transfer without emulator sleep/skip')
dst.with_suffix('.patch.json').write_text(json.dumps(record, indent=2)+'\n')
print(json.dumps(record))
