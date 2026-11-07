#!/usr/bin/env python3
"""Prepare the exact private 6.18 SC8180X GLES layout candidate, offline only.

The 6.18 UBWC configuration uses eight-channel macrotiles and GPU bank15.
Restore the original donor channel extraction instead of retaining the old
four-channel CPU override. Keep bank15 and every other GLES/mapper/Vulkan byte.
This does not change the kernel, a device, the parent runtime or any boot entry.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct

LIB = 'libGLESv2_adreno.so'
PARENT_SUMS_SHA = 'c8df06d61420cc91d39021cfe0b0e4f97011f75f45340ac4b29e8f0abeffde35'
PARENT_SHA = 'd7e41532779ba5be94eae207ce3ad6800489a93ba16ccc1fe7b80aa9778995ec'
OUTPUT_SHA = 'e48bf0099077b91f5856eeabe7f751233eea0021e8c3f8fb1c19ca643ccc3987'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(value, message):
    if not value:
        raise ValueError(message)


def patch_gles(original):
    require(sha(original) == PARENT_SHA, 'Unexpected complete GLES input hash')
    require(original[:6] == b'\x7fELF\x02\x01' and
            struct.unpack_from('<H', original, 18)[0] == 183, 'Expected ELF64 ARM64')
    phoff = struct.unpack_from('<Q', original, 32)[0]
    phsize, phcount = struct.unpack_from('<HH', original, 54)
    require(phsize == 56 and phoff + phsize * phcount <= len(original), 'Invalid ELF program headers')

    def offset(va):
        matches = []
        for i in range(phcount):
            kind, flags, off, address, _, size, _, _ = struct.unpack_from(
                '<IIQQQQQQ', original, phoff + i * phsize)
            if kind == 1 and flags & 1 and address <= va and va + 4 <= address + size:
                require(off + size <= len(original), 'Truncated executable segment')
                matches.append(off + va - address)
        require(len(matches) == 1, 'Instruction must belong to exactly one executable segment')
        return matches[0]

    at, bank = offset(0x2a6100), offset(0x2a61b0)
    require(struct.unpack_from('<I', original, at)[0] == 0x52800086, 'Expected MOV W6,#4')
    require(struct.unpack_from('<I', original, bank)[0] == 0x528001ea, 'Expected retained MOV W10,#15')
    modified = bytearray(original)
    struct.pack_into('<I', modified, at, 0x530a3506)  # Original UBFX W6,W8,#10,#4.
    require([i for i, (a, b) in enumerate(zip(original, modified)) if a != b] ==
            list(range(at, at + 4)), 'Unexpected changed bytes')
    require(sha(modified) == OUTPUT_SHA, 'Unexpected complete candidate hash')
    patch = {'schema': 1, 'format': 'replace-exact-bytes', 'file': LIB,
             'parent_sha256': PARENT_SHA, 'modified_sha256': OUTPUT_SHA,
             'changes': [{'offset': at, 'va': '0x2a6100',
                          'before': original[at:at + 4].hex(),
                          'after': modified[at:at + 4].hex()}]}
    return bytes(modified), patch


def prepare(source, output):
    source, output = source.resolve(), output.absolute()
    patch_path = output.with_name(output.name + '.patch.json')
    require(not output.exists() and not output.is_symlink(), 'Refusing existing candidate')
    require(not patch_path.exists() and not patch_path.is_symlink(), 'Refusing existing patch')
    sums = (source / 'SHA256SUMS').read_bytes()
    require(sha(sums) == PARENT_SUMS_SHA, 'Expected exact 15-library CPU-access parent manifest')
    records = {}
    for line in sums.decode('utf-8').splitlines():
        digest, name = line.split()
        require(Path(name).name == name and name not in records, 'Invalid manifest filename')
        path = source / name
        require(path.is_file() and not path.is_symlink(), 'Expected regular input: ' + name)
        require(sha(path.read_bytes()) == digest, 'Parent hash mismatch: ' + name)
        records[name] = digest
    require(len(records) == 16 and sum(n.endswith('.so') for n in records) == 15 and
            'qgl_config.txt' in records, 'Expected 15 libraries plus qgl_config')
    parent_manifest = (source / 'RUNTIME.json').read_bytes()
    report = json.loads(parent_manifest)
    require(set(report['files']) == set(records), 'RUNTIME/SHA256SUMS file set differs')
    require(all(v['sha256'] == records[n] for n, v in report['files'].items()),
            'RUNTIME/SHA256SUMS hashes differ')
    data, patch = patch_gles((source / LIB).read_bytes())
    output.mkdir(parents=True)
    for name in records:
        shutil.copyfile(source / name, output / name)
    (output / LIB).write_bytes(data)
    report.update(kind='private-full-qualcomm-v77-linux618-layout8-bank15-candidate',
                  parent_runtime=str(source), parent_manifest_sha256=sha(parent_manifest))
    report['files'][LIB].update(sha256=OUTPUT_SHA, operation=
        'Restore original UBFX W6,W8,#10,#4 at VA0x2a6100; keep historical bank15/PBO/zero-area/fence/private compiler patches')
    report['layout_candidate'] = {
        'scope': 'Linux6.18 SC8180X 8-channel GPU programming; do not use as old4-channel default',
        'instruction': 'ubfx w6,w8,#10,#4',
        'field': 'CPU converter+312 from original capability word+80 bits10..13',
        'bank': 'unchanged15', 'actual_newboot_field_sampled': False, 'hardware_verified': False}
    (output / 'RUNTIME.json').write_text(json.dumps(report, indent=2) + '\n')
    (output / 'SHA256SUMS').write_text(''.join(
        sha((output / n).read_bytes()) + '  ' + n + '\n' for n in report['files']))
    with patch_path.open('x') as f:
        f.write(json.dumps(patch, indent=2) + '\n')
    for name, digest in records.items():
        require(sha((source / name).read_bytes()) == digest, 'Parent changed: ' + name)
        require(sha((output / name).read_bytes()) == (OUTPUT_SHA if name == LIB else digest),
                'Output reread mismatch: ' + name)
    require((source / 'RUNTIME.json').read_bytes() == parent_manifest, 'Parent manifest changed')
    require(json.loads(patch_path.read_text()) == patch, 'Patch reread mismatch')
    print('QUEST2_618_LAYOUT_PREPARED libraries=15 modified_libraries=1 changed_bytes=4 device_operations=0')
    print('gles_sha256=' + OUTPUT_SHA)
    print(output.resolve())
    print(patch_path.resolve())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    prepare(args.runtime, args.output)


if __name__ == '__main__':
    main()
