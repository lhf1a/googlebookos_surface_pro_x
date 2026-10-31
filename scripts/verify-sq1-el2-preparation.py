#!/usr/bin/env python3
"""Read-only verification of pinned SQ1 EL2 preparation, never executes EFI."""
import hashlib
import json
from pathlib import Path
import struct
import sys


def require(condition, message):
    if not condition:
        raise ValueError(message)


def verify(directory):
    manifest = json.loads((Path(__file__).resolve().parents[1] /
                           'configs/sq1-el2-preparation.json').read_text())
    data = (directory / 'tcblaunch.exe').read_bytes()
    require(hashlib.sha256(data).hexdigest() == manifest['tcb_sha256'], 'TCB SHA mismatch')
    pe = struct.unpack_from('<I', data, 60)[0]
    opt = pe + 24
    base = struct.unpack_from('<Q', data, opt + 24)[0]
    count = struct.unpack_from('<H', data, pe + 6)[0]
    table = opt + struct.unpack_from('<H', data, pe + 20)[0]
    sections = [struct.unpack_from('<IIII', data, table + i * 40 + 8)
                for i in range(count)]
    for address, expected in manifest['instructions'].items():
        rva = int(address, 16) - base
        matches = [raw + rva - va for _, va, size, raw in sections
                   if va <= rva and rva + 4 <= va + size]
        require(len(matches) == 1, 'Instruction mapping: ' + address)
        require(struct.unpack_from('<I', data, matches[0])[0] == int(expected, 16),
                'Instruction mismatch: ' + address)
    print('PASS TCB SHA256 and 8 instruction witnesses (static only)')
    for name, expected in manifest['efi'].items():
        blob = (directory / 'slbounce' / 'out' / name).read_bytes()
        pe = struct.unpack_from('<I', blob, 60)[0]
        require(blob[:2] == b'MZ' and blob[pe:pe+4] == b'PE\0\0', name + ' PE')
        require(struct.unpack_from('<H', blob, pe+4)[0] == 0xaa64, name + ' ARM64')
        require(struct.unpack_from('<H', blob, pe+24+68)[0] == expected['subsystem'], name + ' subsystem')
        require(len(blob) == expected['bytes'], name + ' size')
        require(hashlib.sha256(blob).hexdigest() == expected['sha256'], name + ' SHA')
        print('PASS ' + name + ' SHA256/size/ARM64/subsystem')
    print('NOT TESTED: firmware acceptance, EL2 Linux, KVM, physical media')


if __name__ == '__main__':
    try:
        require(len(sys.argv) == 2, 'usage: verify-sq1-el2-preparation.py SAMPLE_DIRECTORY')
        verify(Path(sys.argv[1]))
    except (ValueError, OSError, struct.error) as exc:
        print('FAIL: ' + str(exc), file=sys.stderr)
        sys.exit(1)
