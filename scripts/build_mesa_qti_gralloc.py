#!/usr/bin/env python3
"""Build the Mesa u_gralloc_cros_api -> QTI gralloc adapter (gralloc.sq2_mesa.so)."""
import argparse
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'probe/graphics/quest2-mesa-gralloc-adapter.c'
DEFAULT_NDK_CC = Path(
    '/mnt/spx-workspace/toolchains/sq2-graphics/android-ndk-r28c/toolchains/llvm/prebuilt/'
    'linux-x86_64/bin/aarch64-linux-android35-clang'
)
FALLBACK_NDK_CC = Path(
    '/root/.cache/sq2-graphics/android-ndk-r28c/toolchains/llvm/prebuilt/'
    'linux-x86_64/bin/aarch64-linux-android35-clang'
)
ALLOWED_NEEDED = {'liblog.so', 'libdl.so', 'libc.so'}


def find_android_cc(explicit=None):
    """Return the Android NDK AArch64 clang binary path."""
    candidates = []
    if explicit is not None:
        candidates.append(Path(explicit))
    env_cc = os.environ.get('SQ2_ANDROID_CC')
    if env_cc:
        candidates.append(Path(env_cc))
    which_cc = shutil.which('aarch64-linux-android35-clang')
    if which_cc:
        candidates.append(Path(which_cc))
    candidates.extend([DEFAULT_NDK_CC, FALLBACK_NDK_CC])
    for candidate in candidates:
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return candidate
    raise FileNotFoundError(
        'Android NDK aarch64-linux-android35-clang not found; set SQ2_ANDROID_CC or --cc'
    )


def verify_adapter_elf(data, readelf_bin):
    """Verify the compiled adapter is ELF64 AArch64, exports HMI, and links only Bionic libs."""
    if len(data) < 64 or data[:6] != b'\x7fELF\x02\x01':
        raise ValueError('Adapter output is not 64-bit little-endian ELF')
    machine = struct.unpack_from('<H', data, 18)[0]
    if machine != 183:
        raise ValueError(f'Expected EM_AARCH64 (183), got {machine}')
    with tempfile.NamedTemporaryFile(suffix='.so') as tmp:
        tmp_path = Path(tmp.name)
        tmp_path.write_bytes(data)
        dyn = subprocess.check_output([str(readelf_bin), '-d', str(tmp_path)], text=True)
        needed = set()
        for line in dyn.splitlines():
            if '(NEEDED)' in line and 'Shared library: [' in line:
                needed.add(line.split('Shared library: [', 1)[1].split(']', 1)[0])
        unexpected = sorted(needed - ALLOWED_NEEDED)
        if unexpected:
            raise ValueError('Unexpected DT_NEEDED entries in gralloc adapter: ' + ', '.join(unexpected))
        syms = subprocess.check_output([str(readelf_bin), '--dyn-syms', str(tmp_path)], text=True)
        if not any(line.rstrip().endswith(' HMI') for line in syms.splitlines()):
            raise ValueError('Adapter ELF does not export HMI symbol')


def build_to_path(output, cc=None):
    """Compile and strip gralloc.sq2_mesa.so to output and return its bytes."""
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
    with tempfile.TemporaryDirectory(prefix='sq2-mesa-gralloc-') as tmp:
        unstripped = Path(tmp) / 'gralloc.sq2_mesa.unstripped.so'
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
                '-Wl,-soname,gralloc.default.so',
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
    verify_adapter_elf(data, readelf_bin)
    return data


def build_bytes(cc=None):
    """Compile gralloc.sq2_mesa.so in a temporary directory and return its ELF bytes."""
    with tempfile.TemporaryDirectory(prefix='sq2-mesa-gralloc-out-') as tmp:
        return build_to_path(Path(tmp) / 'gralloc.sq2_mesa.so', cc=cc)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    data = build_to_path(args.output, cc=args.cc)
    print(f'SQ2_MESA_QTI_GRALLOC_OK {args.output} ({len(data)} bytes)')


if __name__ == '__main__':
    main()
