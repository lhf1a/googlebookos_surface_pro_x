#!/usr/bin/env python3
"""Build Hybrid-only linear allocation padding; never modify the donor input."""
import hashlib
from pathlib import Path
import subprocess
import tempfile
from build_mesa_qti_gralloc import find_android_cc

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'probe/graphics/quest2-mesa-layout.c'
VENDOR_SHA256 = '0fb5d79eb206bd75d4eb8ae68be04cafe89dc4b0d267567a82a56303023a0724'
VENDOR_NAME = 'libsq2_grallocutils_vendor.so'

def build_payload(vendor_bytes, cc=None):
    if hashlib.sha256(vendor_bytes).hexdigest() != VENDOR_SHA256:
        raise ValueError('Unrecognized QTI gralloc utility; refusing ABI interposition')
    clang = find_android_cc(cc)
    with tempfile.TemporaryDirectory(prefix='sq2-layout-') as temp:
        d = Path(temp)
        original = d / VENDOR_NAME
        original.write_bytes(vendor_bytes)
        subprocess.run(['patchelf', '--set-soname', VENDOR_NAME, str(original)], check=True)
        shim = d / 'libgrallocutils.so'
        subprocess.run([str(clang), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-fPIC', '-shared', str(SOURCE), '-Wl,-soname,libgrallocutils.so',
                        '-Wl,--no-as-needed', '-L' + str(d),
                        '-lsq2_grallocutils_vendor', '-ldl', '-o', str(shim)], check=True)
        subprocess.run([str(clang.parent / 'llvm-strip'), '--strip-unneeded', str(shim)], check=True)
        dynamic = subprocess.check_output(
            [str(clang.parent / 'llvm-readelf'), '-d', str(shim)], text=True)
        if '[' + VENDOR_NAME + ']' not in dynamic:
            raise ValueError('Missing vendor forwarding dependency')
        return {'lib64/libgrallocutils.so': shim.read_bytes(),
                'lib64/sq2-qti-display/libgrallocutils.so': shim.read_bytes(),
                'lib64/' + VENDOR_NAME: original.read_bytes()}
