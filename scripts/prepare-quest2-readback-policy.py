#!/usr/bin/env python3
"""Stage one hash-pinned v77 readback-policy experiment, never a system update.

The finalizer normally overwrites thresholds with {UINT32_MAX, 0}; change only
its immediate to {0, 0}. This removes two threshold restrictions, not all GPU
staging gates. Device identity, capabilities and readback implementation remain.
"""
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'out/quest2-gpu/gles-linked'
OUTPUT = ROOT / 'out/quest2-gpu/gles-linked-readback-policy'
LIB = 'libGLESv2_adreno.so'
EXPECTED = '99d4c18d35e69e8589f2550b242d5f6722031404852b9831e328df8d1778551f'
VA = 0x1c2f9c


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def manifest(name):
    result = {}
    for line in (SOURCE / name).read_text().splitlines():
        digest, filename = line.split()
        require(bool(re.fullmatch(r'[0-9a-f]{64}', digest)), 'Invalid digest')
        require(bool(re.fullmatch(r'[A-Za-z0-9_.-]+', filename)) and
                filename not in ('.', '..') and filename not in result, 'Invalid filename')
        require(not (SOURCE / filename).is_symlink() and sha(SOURCE / filename) == digest,
                'Input hash mismatch: ' + filename)
        result[filename] = digest
    return result


def main():
    require(not OUTPUT.exists(), 'Refusing to overwrite a prior candidate')
    donors = manifest('SHA256SUMS')
    probes = manifest('PROBE_SHA256SUMS')
    original = json.loads((ROOT / 'configs/quest2-gles-v77-inputs.json').read_text(encoding='utf-8-sig'))
    require(set(donors) == {Path(path).name for path in original['files']}, 'Unexpected donor set')
    require(set(probes) == {'gles-readback', 'libqti-mapper3-bridge.so',
                           'libquest2-ion-dmaheap.so', 'libquest2-egl-wrapper.so'}, 'Unexpected adapter set')
    require(donors[LIB] == EXPECTED, 'Patch requires exact linked v77 GLES input')
    data = bytearray((SOURCE / LIB).read_bytes())
    require(data[:6] == b'\x7fELF\x02\x01' and struct.unpack_from('<H', data, 18)[0] == 183,
            'Expected little-endian ELF64/AArch64')
    phoff = struct.unpack_from('<Q', data, 32)[0]
    phsize, phcount = struct.unpack_from('<HH', data, 54)
    require(phsize == 56 and 0 < phcount <= 64, 'Unexpected program headers')
    offsets = []
    for i in range(phcount):
        kind, flags, offset, vaddr, _, filesz, _, _ = struct.unpack_from('<IIQQQQQQ', data, phoff+i*phsize)
        if kind == 1 and flags & 1 and vaddr <= VA and VA + 12 <= vaddr + filesz:
            offsets.append(offset + VA - vaddr)
    require(len(offsets) == 1, 'Patch VA does not map to one executable LOAD')
    offset = offsets[0]
    require(struct.unpack_from('<III', data, offset) == (0x2f00e5e0, 0x5284af84, 0xfc246900),
            'Unexpected finalizer instruction sequence')
    struct.pack_into('<I', data, offset, 0x2f00e400)  # movi d0,#0
    changed = [i for i,(a,b) in enumerate(zip(data, (SOURCE / LIB).read_bytes())) if a != b]
    require(changed == [offset, offset+1], 'Unexpected changed bytes')
    OUTPUT.mkdir()
    for filename in donors | probes:
        shutil.copyfile(SOURCE / filename, OUTPUT / filename)
    (OUTPUT / LIB).write_bytes(data)
    (OUTPUT / 'gles-readback').chmod(0o755)
    for filename, files in (('SHA256SUMS', donors), ('PROBE_SHA256SUMS', probes)):
        (OUTPUT / filename).write_text(''.join(f'{sha(OUTPUT / name)}  {name}\n' for name in files))
    report = dict(reference_only=True, system_deployed=False, input_sha256=EXPECTED,
                  output_sha256=sha(OUTPUT / LIB), instruction_va=hex(VA), file_offset=hex(offset),
                  before='movi d0, #0xffffffff', after='movi d0, #0',
                  fields={'byte_threshold': 0, 'percent_threshold': 0},
                  source_hashes=donors | probes,
                  limits='Private SQ2 experiment. Does not guarantee GPU staging or correct readback.')
    (OUTPUT / 'readback-policy.json').write_text(json.dumps(report, indent=2) + '\n')
    print('QUEST2_READBACK_POLICY_STAGED sha256=' + report['output_sha256'])


if __name__ == '__main__':
    main()
