#!/usr/bin/env python3
"""Build the physical-playback adaptation from the mic-corrected donor HAL.

Does not install anything or select a boot entry. Microphone, external devices,
MMAP, emulator pacing and the ALSA writer are not changed.
"""
import argparse
import hashlib
import struct
import subprocess
import tempfile
from pathlib import Path


BASE = '774cd7e731bd5a5fcda6edbde6e97464bd0e04acc1603c3be7b0dbe7a69561ea'
OUTPUT = '65ecd30b575ab81517d86456386842faa65496075216c8c6ae8d2dd7ef8af6e1'

# Donor 16373615, ModulePrimary::getNominalLatencyMs. The presence check is
# before this range. Use the existing 10ms return for PRIMARY output as well.
LATENCY = """
    ldp w9,w8,[x1,#128]
    cbz w8,input
    cmp w8,#1
    b.ne normal
    tbnz w9,#1,low
    tbnz w9,#12,low
normal: mov w0,#85
    ret
input: ldr w8,[x1,#128]
    tbz w8,#4,normal
low: mov w0,#10
    ret
"""

# Built-in proxy creation, not the framework transfer size. With the stock
# four periods, 5760 frames gives the known working 4 x 1440 physical layout
# at 48kHz. Preserve larger requests and capture; external proxy still receives
# its original boolean argument. Keep the original failure and join targets.
PROXY = """
    ldrb w8,[x21,#200]
    tbz w8,#0,.+(0xd696c-0xd6738)
    ldrb w9,[x20,#12]
    add x8,sp,#32
    add x1,x21,#136
    mov x0,x20
    cmp w9,#1
    b.eq external
    ldr x2,[x21,#112]
    ldr w9,[x20,#8]
    cbnz w9,builtin
    mov x9,#5760
    cmp x2,x9
    csel x2,x9,x2,lo
builtin: bl .+(0xd7e40-0xd676c)
    b end
external: mov w2,#1
    bl .+(0xd8170-0xd6778)
end:
"""


def prepare(data):
    if hashlib.sha256(data).hexdigest() == OUTPUT:
        return data  # Explicit component reuse must not apply the edit twice.
    if hashlib.sha256(data).hexdigest() != BASE:
        raise ValueError('Expected the mic-corrected donor, not a stock or experimental HAL')
    result = bytearray(data)
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        for va, size, asm in [(0x9bc0c, 48, LATENCY), (0xd6734, 72, PROXY)]:
            (tmp/'edit.s').write_text('.text\n' + asm)
            subprocess.run(['aarch64-linux-gnu-as', 'edit.s', '-o', 'edit.o'], cwd=tmp, check=True)
            subprocess.run(['aarch64-linux-gnu-objcopy', '-O', 'binary', '-j', '.text',
                            'edit.o', 'edit.bin'], cwd=tmp, check=True)
            code = (tmp/'edit.bin').read_bytes()
            if len(code) != size:
                raise ValueError('Assembly no longer fits original code range')
            phoff, = struct.unpack_from('<Q', data, 32)
            entsize, count = struct.unpack_from('<HH', data, 54)
            for i in range(count):
                typ, flags, off, addr, _, length = struct.unpack_from('<IIQQQQ', data, phoff+i*entsize)
                if typ == 1 and flags & 1 and addr <= va and va+size <= addr+length:
                    offset = off+va-addr
                    result[offset:offset+size] = code
                    break
            else:
                raise ValueError('Executable segment not found')
    return bytes(result)


def install(bodies):
    """Refresh after runtime replacements, before archive/credential packing."""
    path = 'etc/sq2-audio/audio-hal'
    bodies[path] = prepare(bodies[path])


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('original', type=Path)
    p.add_argument('candidate', type=Path)
    args = p.parse_args()
    candidate = prepare(args.original.read_bytes())
    with args.candidate.open('xb') as stream:
        stream.write(candidate)
    print(hashlib.sha256(candidate).hexdigest(), args.candidate.resolve())
