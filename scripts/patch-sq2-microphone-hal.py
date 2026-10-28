#!/usr/bin/env python3
"""Preserve the configured built-in mic ALSA address in pinned Cuttlefish HAL.

XsdcConversion.cpp::createAudioDevice replaces the XML address with "bottom"
for IN_MICROPHONE. StreamPrimary parses CARD_n_DEV_n, falling back to card0/dev0
otherwise. SQ2 captures on card0/dev1. Skip only the built-in mic overwrite;
leave the stock playback/capture transfer, pacing, and MonoPipe code unchanged.
This bounded binary adaptation is for donor 16373615 only, not a generic patch.
"""
import hashlib
import json
import struct
import sys
from pathlib import Path

src, dst = map(Path, sys.argv[1:3])
data = bytearray(src.read_bytes())
original = 'fd219b1058eccc701de685cf59d6578e887c11182d877bb4e6c3a78bfd73e279'
assert hashlib.sha256(data).hexdigest() == original, 'Stock donor HAL required'
assert data[:6] == b'\x7fELF\x02\x01'
va, target = 0xb6120, 0xb6260
phoff = struct.unpack_from('<Q', data, 32)[0]
entsize, count = struct.unpack_from('<HH', data, 54)
offset = None
for i in range(count):
    typ, flags, off, addr, _, size, _, _ = struct.unpack_from('<IIQQQQQQ', data, phoff+i*entsize)
    if typ == 1 and flags & 1 and addr <= va and va+16 <= addr+size:
        offset = off+va-addr
assert offset is not None
# Exact control-flow context: IN_MICROPHONE=9, non-mic branch skips this block.
assert struct.unpack_from('<II', data, offset-28) == (0x7100267f, 0x54000ac1)
assert struct.unpack_from('<III', data, offset) == (0xb5000a08, 0x90fffae0, 0x9122ac00)
assert data[0x128ab:0x128b2] == b'bottom\0'
after = 0x14000000 | ((target-va)//4 & 0x3ffffff)
struct.pack_into('<I', data, offset, after)
dst.write_bytes(data)
record = dict(original_sha256=original, patched_sha256=hashlib.sha256(data).hexdigest(),
              address=hex(va), file_offset=hex(offset), original='0xb5000a08', patched=hex(after),
              purpose='Preserve Built-In Mic CARD_0_DEV_1; stock transfer/pacing untouched')
dst.with_suffix('.patch.json').write_text(json.dumps(record, indent=2)+'\n')
print(json.dumps(record))
