#!/usr/bin/env python3
"""Build the SDM sysfs backlight compatibility shim and wire it into libsdmcore.so."""
import argparse
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

from build_mesa_qti_gralloc import ALLOWED_NEEDED, find_android_cc

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'probe/graphics/sq2-sdm-backlight-shim.c'
SHIM_NAME = 'libsq2_sdm_backlight.so'
SDMCORE_REL = 'lib64/sq2-qti-display/libsdmcore.so'
SHIM_REL = f'lib64/sq2-qti-display/{SHIM_NAME}'
REQUIRED_SYMBOLS = (
    'sq2_rewrite_panel_backlight_path',
    'sq2_install_sdm_backlight_hooks',
)


def _check_aarch64_elf64(data, label):
    """Validate 64-bit little-endian AArch64 ELF header."""
    if len(data) < 64 or data[:6] != b'\x7fELF\x02\x01':
        raise ValueError(f'{label} is not 64-bit little-endian ELF')
    machine = struct.unpack_from('<H', data, 18)[0]
    if machine != 183:
        raise ValueError(f'{label}: expected EM_AARCH64 (183), got {machine}')


def verify_shim_elf(data, readelf_bin):
    """Verify libsq2_sdm_backlight.so ELF metadata, SONAME, DT_NEEDED, and exports."""
    _check_aarch64_elf64(data, SHIM_NAME)
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
            raise ValueError('Unexpected DT_NEEDED in backlight shim: ' + ', '.join(unexpected))
        if sonames != {SHIM_NAME}:
            raise ValueError(f'Expected DT_SONAME [{SHIM_NAME}], got {sorted(sonames)}')
        syms = subprocess.check_output([str(readelf_bin), '--dyn-syms', str(tmp_path)], text=True)
        for sym in REQUIRED_SYMBOLS:
            if not any(line.rstrip().endswith(' ' + sym) for line in syms.splitlines()):
                raise ValueError('Backlight shim ELF does not export ' + sym)


def build_to_path(output, cc=None):
    """Compile and strip libsq2_sdm_backlight.so to output and return its bytes."""
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
    with tempfile.TemporaryDirectory(prefix='sq2-sdm-backlight-') as tmp:
        unstripped = Path(tmp) / f'{SHIM_NAME}.unstripped.so'
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
                f'-Wl,-soname,{SHIM_NAME}',
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
    verify_shim_elf(data, readelf_bin)
    return data


def build_bytes(cc=None):
    """Compile libsq2_sdm_backlight.so in a temporary directory and return its ELF bytes."""
    with tempfile.TemporaryDirectory(prefix='sq2-sdm-backlight-out-') as tmp:
        return build_to_path(Path(tmp) / SHIM_NAME, cc=cc)


def patch_sdmcore_elf(libsdmcore_bytes):
    """Add DT_NEEDED libsq2_sdm_backlight.so to libsdmcore.so if not already present."""
    _check_aarch64_elf64(libsdmcore_bytes, SDMCORE_REL)
    patchelf = shutil.which('patchelf')
    if not patchelf:
        raise FileNotFoundError('patchelf executable not found in PATH')

    with tempfile.TemporaryDirectory(prefix='sq2-sdmcore-patch-') as tmp:
        target = Path(tmp) / 'libsdmcore.so'
        target.write_bytes(libsdmcore_bytes)
        needed = subprocess.check_output([patchelf, '--print-needed', str(target)], text=True)
        if SHIM_NAME not in needed.splitlines():
            subprocess.run(
                [patchelf, '--page-size', '16384', '--add-needed', SHIM_NAME, str(target)],
                check=True,
            )
            needed = subprocess.check_output([patchelf, '--print-needed', str(target)], text=True)
            if SHIM_NAME not in needed.splitlines():
                raise RuntimeError('Failed to add DT_NEEDED ' + SHIM_NAME + ' to libsdmcore.so')
        return target.read_bytes()


def build_payload(libsdmcore_bytes, cc=None):
    """Return Hybrid root payload overrides for SDM sysfs backlight compatibility."""
    return {
        SDMCORE_REL: patch_sdmcore_elf(libsdmcore_bytes),
        SHIM_REL: build_bytes(cc=cc),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    data = build_to_path(args.output, cc=args.cc)
    print(f'SQ2_SDM_BACKLIGHT_SHIM_OK {args.output} ({len(data)} bytes)')


if __name__ == '__main__':
    main()
