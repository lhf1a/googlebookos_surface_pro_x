#!/usr/bin/env python3
"""Inspect ELF32 Qualcomm video firmware without executing or authenticating it."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('firmware', type=Path)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
b = a.firmware.read_bytes()
assert b[:6] == b'\x7fELF\x01\x01', 'expected little-endian ELF32'
assert len(b) >= 52
hdr = struct.unpack_from('<16sHHIIIIIHHHHHH', b)
assert hdr[2] == 40, 'expected ARM firmware'
phoff, entsize, count = hdr[5], hdr[9], hdr[10]
assert entsize == 32 and 0 < count <= 256
assert phoff + count * entsize <= len(b)
segments = []
for i in range(count):
    typ, off, va, pa, fs, ms, flags, align = struct.unpack_from('<8I', b, phoff + i * entsize)
    assert off + fs <= len(b), 'split/incomplete image is not accepted'
    if typ == 1:
        assert fs <= ms and pa + ms <= 2**32
        segments.append(dict(index=i, offset=off, physical=pa, filesz=fs, memsz=ms,
                             flags=flags, alignment=align, qcom_hash=((flags >> 24) & 7) == 2,
                             relocatable=bool(flags & (1 << 27))))
loads = [s for s in segments if not s['qcom_hash'] and s['memsz']]
assert loads
lo = min(s['physical'] for s in loads)
hi = max(s['physical'] + s['memsz'] for s in loads)
result = dict(sha256=hashlib.sha256(b).hexdigest(), bytes=len(b),
              versions=[s.decode('ascii') for s in re.findall(rb'QC_IMAGE_VERSION_STRING=[ -~]+', b)],
              segments=segments, load_base=lo, load_end=hi,
              page_rounded_span=((hi+4095)&~4095) - lo,
              status='offline-layout-only-not-authenticated-or-boot-tested')
a.output.write_text(json.dumps(result, indent=2)+'\n')
print(json.dumps(result, indent=2))
