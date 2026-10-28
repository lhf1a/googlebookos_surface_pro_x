#!/usr/bin/env python3
"""Build a Vulkan-only integration candidate from pinned private inputs.

No device writes, shader capture, APK injection or EGL/GLES replacement.
This candidate is not Android CTS certified; remaining gates are documented.
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import argparse

parser=argparse.ArgumentParser()
parser.add_argument('--ahb-allocate', action='store_true', help='Experimental RGBA AHB allocation candidate')
args=parser.parse_args()

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('base', root/'scripts/prepare-quest2-vulkan-only.py')
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
expected = dict(base.EXPECTED)
expected['vulkan-v77-bridge.so'] = 'bcb9b506dc2a4b1c4a53e85c08057d7cb5a0e6e3bd2bd97a90b92de20eb76d42'
source = root/'out/quest2-gpu/vulkan-only-counter-v1'
out = root/('out/quest2-gpu/vulkan-runtime-v3' if args.ahb_allocate else 'out/quest2-gpu/vulkan-runtime-v2')
if out.exists(): raise SystemExit('Refusing existing output')
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
platform=root/'out/quest2-gpu/platform-link'
if args.ahb_allocate:
    for name,digest in {
        'libc++.so':'cb118e98c74d3454858921123b9b51ba13df9cad7dfa141dd892c453661a4d78',
        'libhardware.so':'89869693c2bbb28c49d91b05110698be757d243a6d6b929488abb3f804c368de'
    }.items():
        if (platform/name).is_symlink() or sha(platform/name)!=digest:
            raise SystemExit('Platform link input mismatch: '+name)
for name, digest in expected.items():
    p = source/name
    if p.is_symlink() or sha(p) != digest: raise SystemExit('Input mismatch: '+name)
work = Path(os.environ.get('SQ2_GRAPHICS_WORK', '/root/.cache/sq2-graphics'))
cc = work/'android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang'
out.mkdir()
for name in expected: shutil.copy2(source/name, out/name)
extra=[]
if args.ahb_allocate:
    extra=['-DSQ2_VK_AHB_ALLOCATE']
    subprocess.run([str(cc).replace('clang','clang++'), '-std=c++17','-O2','-fPIC','-shared',
        '-fno-rtti','-nostdlib++','-Wall','-Wextra','-Werror',*extra,
        '-I'+str(work/'src/mesa-26.2.4/include/android_stub'),
        '-I'+str(work/'src/mesa-26.2.4/src/util/u_gralloc'),
        str(root/'probe/graphics/quest2-v77-ahb.cpp'),'-L'+str(platform),
        '-Wl,--no-undefined,-z,global','-lc++','-ldl','-lhardware','-landroid',
        '-o',str(out/'libquest2-v77-ahb.so')],check=True)
    subprocess.run(['patchelf','--replace-needed','libandroid.so','libnativewindow.so',
        str(out/'libquest2-v77-ahb.so')],check=True)
subprocess.run([str(cc), '-std=c11', '-O2', '-fPIC', '-shared', '-Wall', '-Wextra', '-Werror',
    '-DSQ2_VK_PROCESS_READ', '-DSQ2_VK_NATIVE_SRGB', '-DSQ2_VK_AHB_REEXPORT',
    *extra,
    '-I'+str(work/'src/mesa-26.2.4/include/android_stub'),
    str(root/'probe/graphics/quest2-vulkan-wrapper.c'), '-L'+str(out),
    '-Wl,--no-undefined,-Bsymbolic', '-lquest2-v77-ahb', '-ldl',
    '-o', str(out/'libquest2-vulkan-wrapper.so')], check=True)
(out/'qgl_config.txt').write_bytes(b'0x0=0x08675309\nRenderMode=Direct\n')
hashes = {name: sha(out/name) for name in sorted([*expected, 'qgl_config.txt'])}
(out/'SHA256SUMS').write_text(''.join(f'{digest}  {name}\n' for name,digest in hashes.items()))
(out/'candidate.json').write_text(json.dumps({
    'sha256': hashes, 'system_loader': True, 'mesa_egl_preserved': True,
    'shader_capture': False, 'app_diagnostics': False,
    'actual_vulkan_version': '1.1.295', 'required_feature_version': 4198400,
    'cts_passed': False, 'boot_validated': False, 'experimental_ahb_allocate': args.ahb_allocate,
    'open_gates': ([] if args.ahb_allocate else ['new AHB export allocation']) +
                  ['additional native/AHB formats', 'cached non-coherent memory type 2',
                   'Chrome pipeline failures', 'persistent boot and capability XML']
}, indent=2)+'\n')
print('VULKAN_RUNTIME_CANDIDATE_BUILT', hashes['libquest2-vulkan-wrapper.so'])
