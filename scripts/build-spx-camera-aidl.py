#!/usr/bin/env python3
"""Build frozen AOSP camera Binder interfaces for the native SPX provider."""
import argparse
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('interfaces', type=Path)
p.add_argument('aidl', type=Path)
p.add_argument('ndk_llvm', type=Path)
p.add_argument('output', type=Path)
p.add_argument('--binder-headers', type=Path, required=True)
a = p.parse_args()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=False)
interfaces = [
    ('common', 'android.hardware.common', 2),
    ('common/fmq', 'android.hardware.common.fmq', 1),
    # Camera imports only graphics enums; frozen V5 contains every value used here.
    # This source revision calls its un-frozen, additive enum update V6.
    ('graphics/common', 'android.hardware.graphics.common', 5),
    ('camera/common', 'android.hardware.camera.common', 1),
    ('camera/metadata', 'android.hardware.camera.metadata', 3),
    ('camera/device', 'android.hardware.camera.device', 3),
    ('camera/provider', 'android.hardware.camera.provider', 3),
]
roots = [a.interfaces.resolve()/part/'aidl/aidl_api'/name/str(version)
         for part, name, version in interfaces]
for directory in roots:
    assert (directory/'.hash').is_file(), directory
    digest = (directory/'.hash').read_text().strip().splitlines()[-1]
    assert len(digest) == 40
    for src in sorted(directory.rglob('*.aidl')):
        subprocess.run([str(a.aidl.resolve()), '--lang=ndk', '--structured',
                        '--stability=vintf', '--version='+directory.name,
                        '--hash='+digest, '--out='+str(out/'src'),
                        '--header_out='+str(out/'include')] +
                       ['-I'+str(d) for d in roots] + [str(src)], check=True)
compiler = a.ndk_llvm.resolve()/'bin/aarch64-linux-android35-clang++'
objects = []
for i, src in enumerate(sorted((out/'src').rglob('*.cpp'))):
    obj = out/(str(i)+'.o')
    subprocess.run([str(compiler), '-std=c++17', '-O2', '-fPIC', '-DBINDER_STABILITY_SUPPORT',
                    '-I'+str(out/'include'),
                    '-I'+str(a.binder_headers.resolve()/'include_cpp'),
                    '-I'+str(a.binder_headers.resolve()/'include_platform'), '-c', str(src), '-o', str(obj)], check=True)
    objects.append(str(obj))
assert objects
subprocess.run([str(a.ndk_llvm.resolve()/'bin/llvm-ar'), 'rcs',
                str(out/'libspx-camera-aidl.a')] + objects, check=True)
print('built', len(objects), 'AIDL objects')
