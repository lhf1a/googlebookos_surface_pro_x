#!/usr/bin/env python3
"""Build/test a manual GET_INFO EFI transport object. No image/boot/device I/O.

Use the original pinned qebspil source archive and a built AArch64 GNU-EFI tree.
Creates a fresh output directory; never downloads, deploys or changes firmware.
The protocol and cache sources are hash checked before compiling. QEMU runs
the transport with environment mocks, not secure-world code or EFI firmware.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PINS = {
    'src/external/EFIScm.h': '022ff767c7725d65b4f4fe140bcb5dc20760c7a8e79953293a9dd62287f2efd3',
    'src/external/EFIListenerDef.h': '622a1a720acb010e502c84f73f2ba25c0ab36b747d130f2c989180fe64ce19ee',
    'src/external/cache.c': '793e9313de422420be696efc5a09cb66224702c7d8c58e0671fe17fe145712da',
    'src/external/CacheMaintenanceLib.h': '07e34ded40094ffe29ffcf2b1f7d9a3f013530237f171252fa54a692455a4ef3',
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qebspil', type=Path, required=True)
    parser.add_argument('--gnu-efi', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    q, e, out = args.qebspil.resolve(), args.gnu_efi.resolve(), args.output.resolve()
    for name, expected in PINS.items():
        if hashlib.sha256((q / name).read_bytes()).hexdigest() != expected:
            parser.error(f'pinned source mismatch: {name}')
    if not (e / 'inc/aarch64/efibind.h').is_file():
        parser.error('GNU-EFI AArch64 headers missing')
    if out.exists():
        parser.error('output exists; use fresh space to preserve previous evidence')
    out.mkdir(parents=True)
    records = []

    def run(name, command):
        command = [str(x) for x in command]
        result = subprocess.run(command, capture_output=True, timeout=90)
        (out / f'{name}.stdout').write_bytes(result.stdout)
        (out / f'{name}.stderr').write_bytes(result.stderr)
        (out / f'{name}.exit').write_text(str(result.returncode) + '\n')
        records.append({'name': name, 'command': command, 'exit': result.returncode})
        (out / 'commands.json').write_text(json.dumps(records, indent=2) + '\n')
        if result.returncode:
            raise SystemExit(f'{name} failed: exit {result.returncode}; see {out}')
        return result.stdout

    flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-fshort-wchar']
    common = ['-I', e / 'inc', '-I', q / 'src']
    source = ROOT / 'tests/surface_scm_getinfo_efi_test.c'
    run('host-build', ['gcc', *flags, '-O1', '-g', '-fno-pie', '-no-pie',
                      '-fsanitize=address,undefined', *common, '-I', e / 'inc/x86_64',
                      source, '-o', out / 'transport-host'])
    host = run('host-run', [out / 'transport-host'])
    run('arm64-build', ['aarch64-linux-gnu-gcc', *flags, '-O2', '-static', *common,
                       '-I', e / 'inc/aarch64', source, '-o', out / 'transport-arm64'])
    arm = run('arm64-run', ['qemu-aarch64', out / 'transport-arm64'])
    assert host == arm and b'NO hardware calls' in arm
    release = ['aarch64-linux-gnu-gcc', *flags, '-O2', '-ffreestanding',
               '-fno-stack-protector', '-fpic', *common, '-I', e / 'inc/aarch64', '-c']
    run('release-build', [*release, ROOT / 'probe/lte/surface-scm-getinfo-efi.c',
                          '-o', out / 'surface-scm-getinfo-efi.o'])
    run('cache-build', [*release, '-DSTATIC=static', q / 'src/external/cache.c', '-o', out / 'cache.o'])
    run('combined-object', ['aarch64-linux-gnu-ld', '-r',
                            out / 'surface-scm-getinfo-efi.o', out / 'cache.o',
                            '-o', out / 'surface-scm-getinfo-combined.o'])
    asm = run('release-disassembly', ['aarch64-linux-gnu-objdump', '-d',
                                      out / 'surface-scm-getinfo-combined.o']).lower()
    assert b'currentel' in asm and b'ivac' in asm and b'civac' in asm
    symbols = run('release-undefined', ['aarch64-linux-gnu-nm', '-u',
                                       out / 'surface-scm-getinfo-combined.o']).decode()
    assert all(line.split()[-1] in ('memcpy', 'memset', 'memcmp')
               for line in symbols.splitlines() if line.strip()), symbols
    manifest = {
        'qebspil_commit': '8e4d9e676a3b3afe136cda9b953a2139ff1a32d0',
        'pinned_inputs': PINS, 'hardware_access': False,
        'artifact': 'relocatable EFI transport object, NOT a bootable image',
        'response_policy': 'conservative completed=1, full return slot=0; not hardware-confirmed',
        'tests': '17 transport cases plus wrong-EL/revision/null gates, host and ARM64 identical',
        'hashes': {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                   for p in [ROOT / 'probe/lte/surface-scm-storage-read.h',
                             ROOT / 'probe/lte/surface-scm-getinfo-efi.c',
                             ROOT / 'probe/lte/surface-scm-getinfo-efi.h', source,
                             out / 'surface-scm-getinfo-combined.o']},
    }
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
