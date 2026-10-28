#!/usr/bin/env python3
"""Pinned Cuttlefish 16373615 HAL workaround for physical SQ2 ALSA.

Use a backpressured MonoPipe and bypass StreamPrimary's emulator sleep/skip
loop. Enqueue success alone is not physical playback: the original output pipe
was nonblocking and could discard frames while acknowledging the full count.
Preserve error and stub paths and the original blocking capture behavior.
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

# StreamAlsa::start -> MonoPipe(bufferFrames, format, writeCanBlock).
# The stock call uses mIsInput, so only capture blocks. Without emulator pacing,
# nonblocking playback drops queued frames and accelerates the media clock.
pipe_va = 0xd4784
pipe_offset = None
for i in range(count):
    typ, flags, off, addr, _, size, _, _ = struct.unpack_from('<IIQQQQQQ', data, phoff+i*entsize)
    if typ == 1 and flags & 1 and addr <= pipe_va and pipe_va+12 <= addr+size:
        pipe_offset = off+pipe_va-addr
assert pipe_offset is not None
pipe_before = struct.unpack_from('<I', data, pipe_offset)[0]
assert pipe_before == 0x2a1803e3, hex(pipe_before)  # mov w3, w24 (mIsInput)
assert struct.unpack_from('<I', data, pipe_offset+8)[0] == 0x94011751  # MonoPipe ctor
pipe_after = 0x52800023  # mov w3, #1; both directions now have blocking writers
struct.pack_into('<I', data, pipe_offset, pipe_after)
dst.write_bytes(data)
record = dict(original_sha256=expected, patched_sha256=hashlib.sha256(data).hexdigest(),
              address=hex(va), file_offset=hex(offset), original=hex(before), patched=hex(after),
              pipe_address=hex(pipe_va), pipe_file_offset=hex(pipe_offset),
              pipe_original=hex(pipe_before), pipe_patched=hex(pipe_after),
              purpose='Backpressure the ALSA pipe and bypass emulator sleep/skip without dropping acknowledged playback frames')
dst.with_suffix('.patch.json').write_text(json.dumps(record, indent=2)+'\n')
print(json.dumps(record))
