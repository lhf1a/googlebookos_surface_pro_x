#!/usr/bin/env python3
"""Prepare the private Linux 6.18 SC8180X GLES Direct-rendering workaround.

Large HWUI windows misrender with the original multi-bin path. Initialize the
existing explicit Direct mode instead; retain all API/capability fields and the
CPU-layout/upload fixes. This is not a GMEM root-cause repair. The pinned runtime
is for the Surface SQ1/SQ2 port, not a general donor-driver update.
No device operations, cache changes or replacement of existing artifacts.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct

LIB = 'libGLESv2_adreno.so'
PARENT_SUMS_SHA = 'f3d01db0714559b03b0ff8bc84d3d193f171f0466d4efdaa0c3f45c1d23c6ed4'
PARENT_SHA = 'e48bf0099077b91f5856eeabe7f751233eea0021e8c3f8fb1c19ca643ccc3987'
OUTPUT_SHA = '58e1aafe2de28dfb632df9c64b56b761906bf8aeae014975f71938f1061a4d83'
VA = 0x167734
BEFORE = bytes.fromhex('a2791c1244ab3db9')
AFTER = bytes.fromhex('a2011d325fab3db9')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(ok, message):
    if not ok:
        raise ValueError(message)


def patch_gles(original):
    require(sha(original) == PARENT_SHA, 'Unexpected complete GLES input hash')
    require(len(original) >= 64 and original[:6] == b'\x7fELF\x02\x01' and
            struct.unpack_from('<H', original, 18)[0] == 183, 'Expected ELF64 ARM64')
    phoff = struct.unpack_from('<Q', original, 32)[0]
    phsize, phcount = struct.unpack_from('<HH', original, 54)
    require(phsize == 56 and phoff + phcount * phsize <= len(original), 'Invalid ELF program headers')
    matches = []
    for i in range(phcount):
        kind, flags, off, address, _, size, _, _ = struct.unpack_from(
            '<IIQQQQQQ', original, phoff + i * phsize)
        if kind == 1 and (flags & 7) == 5 and address <= VA and VA + len(BEFORE) <= address + size:
            require(off + size <= len(original), 'Truncated executable segment')
            matches.append(off + VA - address)
    require(len(matches) == 1, 'Expected unique executable instruction range')
    at = matches[0]
    require(original[at:at+8] == BEFORE, 'Unexpected context initializer')
    # shortcut: force Direct until the large-window GMEM path is repaired and verified.
    # AND w2,w13,~8 -> ORR w2,w13,8: enable the existing explicit mode bit.
    # STR w4,[x26,15784] -> STR wzr,[x26,15784]: Direct=0, not default=1.
    # The following original STR w2,[x26,136] commits the flag unchanged.
    candidate = original[:at] + AFTER + original[at+8:]
    require(sha(candidate) == OUTPUT_SHA, 'Unexpected complete candidate hash')
    return candidate, dict(offset=at, va=hex(VA), before=BEFORE.hex(), after=AFTER.hex(),
                          parent_sha256=PARENT_SHA, modified_sha256=OUTPUT_SHA)


def prepare(source, output):
    source, output = source.resolve(), output.absolute()
    patch_path = output.with_name(output.name + '.patch.json')
    require(not output.exists() and not output.is_symlink(), 'Fresh output required')
    require(not patch_path.exists() and not patch_path.is_symlink(), 'Fresh patch path required')
    sums = (source/'SHA256SUMS').read_bytes()
    require(sha(sums) == PARENT_SUMS_SHA, 'Expected exact layout8/bank15 parent manifest')
    files = {}
    for line in sums.decode().splitlines():
        digest, name = line.split()
        require(Path(name).name == name and name not in files, 'Invalid manifest filename')
        path = source/name
        require(path.is_file() and not path.is_symlink(), 'Expected regular input: ' + name)
        require(sha(path.read_bytes()) == digest, 'Parent hash mismatch: ' + name)
        files[name] = digest
    require(len(files) == 16 and sum(n.endswith('.so') for n in files) == 15 and
            'qgl_config.txt' in files, 'Expected 15 libraries plus qgl_config')
    manifest = (source/'RUNTIME.json').read_bytes()
    report = json.loads(manifest)
    require(set(report['files']) == set(files) and
            all(v['sha256'] == files[n] for n, v in report['files'].items()),
            'RUNTIME/SHA256SUMS mismatch')
    candidate, patch = patch_gles((source/LIB).read_bytes())
    output.mkdir(parents=True)
    for name in files:
        shutil.copyfile(source/name, output/name)
    (output/LIB).write_bytes(candidate)
    report.update(kind='private-full-qualcomm-v77-linux618-layout8-direct-candidate',
                  parent_runtime=str(source), parent_manifest_sha256=sha(manifest))
    report['files'][LIB].update(sha256=OUTPUT_SHA, operation='Initialize existing explicit Direct mode; preserve capabilities and prior patches')
    report['direct_rendering'] = dict(scope='Surface SC8180X Linux6.18 private runtime',
                                    gmem_root_cause_fixed=False, hardware_tested_by_builder=False,
                                    performance_and_power_verified=False, patch=patch)
    (output/'RUNTIME.json').write_text(json.dumps(report, indent=2) + '\n')
    (output/'SHA256SUMS').write_text(''.join(
        sha((output/n).read_bytes()) + '  ' + n + '\n' for n in files))
    patch_path.write_text(json.dumps(patch, indent=2) + '\n')
    for name, digest in files.items():
        require(sha((source/name).read_bytes()) == digest, 'Parent changed: ' + name)
        require(sha((output/name).read_bytes()) == (OUTPUT_SHA if name == LIB else digest),
                'Output mismatch: ' + name)
    require((source/'RUNTIME.json').read_bytes() == manifest, 'Parent manifest changed')
    print('QUEST2_618_DIRECT_PREPARED libraries=15 modified_libraries=1 device_operations=0')
    print('gles_sha256=' + OUTPUT_SHA)
    print(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    prepare(args.runtime, args.output)


if __name__ == '__main__':
    main()
