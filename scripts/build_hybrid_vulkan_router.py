#!/usr/bin/env python3
"""Build the SQ2Hyb Dual-Vulkan HAL router (vulkan.freedreno.so)."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile

from build_mesa_qti_gralloc import ALLOWED_NEEDED, find_android_cc

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'probe/graphics/sq2-hybrid-vulkan-router.c'
SONAME = 'vulkan.freedreno.so'
REQUIRED_SYMBOLS = (
    'HMI',
    'sq2_vk_classify_process',
    'sq2_vk_type_name',
    'sq2_vk_evaluate_policy',
)


def verify_router_elf(data, readelf_bin):
    """Verify ELF64 AArch64 metadata, SONAME, DT_NEEDED, and exported symbols."""
    if len(data) < 64 or data[:6] != b'\x7fELF\x02\x01':
        raise ValueError('Router output is not 64-bit little-endian ELF')
    machine = struct.unpack_from('<H', data, 18)[0]
    if machine != 183:
        raise ValueError(f'Expected EM_AARCH64 (183), got {machine}')
    with tempfile.NamedTemporaryFile(suffix='.so') as tmp:
        tmp_path = Path(tmp.name)
        tmp_path.write_bytes(data)
        dyn = subprocess.check_output([str(readelf_bin), '-d', str(tmp_path)], text=True)
        needed = set()
        sonames = set()
        for line in dyn.splitlines():
            if '(NEEDED)' in line and 'Shared library: [' in line:
                needed.add(line.split('Shared library: [', 1)[1].split(']', 1)[0])
            elif '(SONAME)' in line and 'Library soname: [' in line:
                sonames.add(line.split('Library soname: [', 1)[1].split(']', 1)[0])
        unexpected = sorted(needed - ALLOWED_NEEDED)
        if unexpected:
            raise ValueError('Unexpected DT_NEEDED entries in router: ' + ', '.join(unexpected))
        if sonames != {SONAME}:
            raise ValueError(f'Expected DT_SONAME [{SONAME}], got {sorted(sonames)}')
        syms = subprocess.check_output([str(readelf_bin), '--dyn-syms', str(tmp_path)], text=True)
        for sym in REQUIRED_SYMBOLS:
            if not any(line.rstrip().endswith(' ' + sym) for line in syms.splitlines()):
                raise ValueError('Router ELF does not export required symbol: ' + sym)


def build_to_path(output, cc=None):
    """Compile and strip vulkan.freedreno.so to output and return its ELF bytes."""
    clang = find_android_cc(cc)
    bin_dir = clang.parent
    strip_bin = bin_dir / 'llvm-strip'
    readelf_bin = bin_dir / 'llvm-readelf'
    if not strip_bin.is_file():
        raise FileNotFoundError('llvm-strip missing alongside ' + str(clang))
    if not readelf_bin.is_file():
        raise FileNotFoundError('llvm-readelf missing alongside ' + str(clang))

    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='sq2-vk-router-') as tmp:
        unstripped = Path(tmp) / f'{SONAME}.unstripped.so'
        subprocess.run(
            [
                str(clang),
                '-std=c11',
                '-O2',
                '-fPIC',
                '-shared',
                '-Wall',
                '-Wextra',
                '-Werror',
                '-pthread',
                f'-Wl,-soname,{SONAME}',
                '-Wl,--no-undefined',
                str(SOURCE),
                '-llog',
                '-ldl',
                '-o',
                str(unstripped),
            ],
            check=True,
        )
        subprocess.run(
            [str(strip_bin), '--strip-unneeded', '-o', str(output), str(unstripped)],
            check=True,
        )
    data = output.read_bytes()
    verify_router_elf(data, readelf_bin)
    return data


def build_bytes(cc=None):
    """Compile vulkan.freedreno.so in a temporary directory and return its ELF bytes."""
    with tempfile.TemporaryDirectory(prefix='sq2-vk-router-out-') as tmp:
        return build_to_path(Path(tmp) / SONAME, cc=cc)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    data = build_to_path(args.output, cc=args.cc)
    print(f'SQ2_HYBRID_VULKAN_ROUTER_OK {args.output} ({len(data)} bytes)')


if __name__ == '__main__':
    main()
