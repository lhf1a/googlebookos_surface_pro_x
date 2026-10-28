#!/usr/bin/env python3
"""Experimental v77 cached allocation on MSM's real coherent cached backing.

SQ2 kernel accepts MSM_BO_CACHED_COHERENT. Only the previously rejected GSL
cache mode 0x0c000000 is changed; other modes and error/ownership paths remain.
This requires real host/GPU coherence tests before system deployment.
"""
import hashlib
import importlib.util
from pathlib import Path
import shutil
import struct
import argparse
import subprocess
import tempfile

parser=argparse.ArgumentParser()
parser.add_argument('--scoped',action='store_true')
args=parser.parse_args()

root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('base',root/'scripts/prepare-quest2-vulkan-only.py')
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
expected=dict(base.EXPECTED)
expected.update({
 'vulkan-v77-bridge.so':'bcb9b506dc2a4b1c4a53e85c08057d7cb5a0e6e3bd2bd97a90b92de20eb76d42',
 'libquest2-v77-ahb.so':'050cadeb3958c4cd5ca148285186e3a23fa6ad5e59e35ca0faf56f246fc4bd91',
 'libquest2-vulkan-wrapper.so':'69b710019872e1d622e797a35f8d4f636e9a0439028d0e6815ac0c70fa389b7f',
 'qgl_config.txt':'1f93a7a1446b2e934f12f23aa55514ee41852bf6df7851788d053cbd90e1f008'})
source=root/'out/quest2-gpu/vulkan-runtime-v3'
out=root/('out/quest2-gpu/vulkan-runtime-v5' if args.scoped else 'out/quest2-gpu/vulkan-runtime-v4')
if out.exists():raise SystemExit('Refusing existing output')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for name,digest in expected.items():
 if (source/name).is_symlink() or sha(source/name)!=digest:raise SystemExit('Input mismatch: '+name)
data=bytearray((source/'libgsl.so').read_bytes())
def word(at,value):
 if struct.unpack_from('<I',data,at)[0]!=value:raise SystemExit(f'Instruction mismatch at {at:x}')
for at,value in {0x20f8fc:0x120606e8,0x20f900:0x52a1000b,0x20f908:0x1a8803ea,
                 0x20f90c:0x3400028a,0x20f910:0x52a0800c,0x20f918:0x54001561,
                 0x20fbc4:0x90fff141,0x20fbc8:0xf0fff104,0x20fbcc:0x90fff185}.items():word(at,value)
# Mask has four possible values. 0/8 take WC; 4 takes uncached; only C reaches
# this branch. Reuse its former error block, preserving the common tail.
replacement=struct.pack('<III',0x2a1f03f8,0x52a00115,
    0x14000000|(((0x20f924-0x20fbcc)//4)&0x3ffffff))
data[0x20fbc4:0x20fbd0]=replacement
original=(source/'libgsl.so').read_bytes()
if len(data)!=len(original) or any(a!=b and not 0x20fbc4<=i<0x20fbd0
    for i,(a,b) in enumerate(zip(original,data))):raise SystemExit('Unexpected byte changes')
with tempfile.TemporaryDirectory(prefix='sq2-cache-policy-') as directory:
    tmp=Path(directory);(tmp/'gsl.so').write_bytes(data)
    subprocess.run(['aarch64-linux-gnu-gcc','-static','-O2','-Wall','-Wextra','-Werror',
        str(root/'probe/graphics/quest2-cache-policy-test.c'),str(root/'probe/graphics/quest2-cache-policy-test.S'),
        '-o',str(tmp/'check')],check=True)
    result=subprocess.check_output(['qemu-aarch64',str(tmp/'check'),str(tmp/'gsl.so')],text=True,timeout=30).strip()
    if result!='CACHE_POLICY_QEMU_PASS cases=512':raise SystemExit('Cache policy test failed')
    print(result)
out.mkdir()
for name in expected:shutil.copy2(source/name,out/name)
(out/'libgsl.so').write_bytes(data)
if args.scoped:
    donor=bytearray((out/'vulkan-v77-bridge.so').read_bytes())
    before=b'gsl_memory_alloc_pure_64\0';after=b'sq2_memory_alloc_pure_64\0'
    if donor.count(before)!=1 or len(before)!=len(after):raise SystemExit('Unexpected import name')
    donor=donor.replace(before,after)
    (out/'vulkan-v77-bridge.so').write_bytes(donor)
    work=Path('/root/.cache/sq2-graphics')
    cc=work/'android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang'
    platform=root/'out/quest2-gpu/platform-link'
    for name,digest in {'libc++.so':'cb118e98c74d3454858921123b9b51ba13df9cad7dfa141dd892c453661a4d78',
                         'libhardware.so':'89869693c2bbb28c49d91b05110698be757d243a6d6b929488abb3f804c368de'}.items():
        if (platform/name).is_symlink() or sha(platform/name)!=digest:raise SystemExit('Platform mismatch: '+name)
    flags=['-O2','-fPIC','-shared','-Wall','-Wextra','-Werror','-DSQ2_VK_AHB_ALLOCATE','-DSQ2_VK_CACHED_MEMORY',
        '-I'+str(work/'src/mesa-26.2.4/include/android_stub')]
    subprocess.run([str(cc).replace('clang','clang++'),'-std=c++17','-fno-rtti','-nostdlib++',*flags,
        '-I'+str(work/'src/mesa-26.2.4/src/util/u_gralloc'),str(root/'probe/graphics/quest2-v77-ahb.cpp'),
        str(root/'probe/graphics/quest2-gsl-cached.cpp'),'-L'+str(platform),'-Wl,--no-undefined,-z,global',
        '-lc++','-ldl','-lhardware','-landroid','-o',str(out/'libquest2-v77-ahb.so')],check=True)
    subprocess.run(['patchelf','--replace-needed','libandroid.so','libnativewindow.so',str(out/'libquest2-v77-ahb.so')],check=True)
    subprocess.run([str(cc),'-std=c11',*flags,'-DSQ2_VK_PROCESS_READ','-DSQ2_VK_NATIVE_SRGB','-DSQ2_VK_AHB_REEXPORT',
        str(root/'probe/graphics/quest2-vulkan-wrapper.c'),'-L'+str(out),'-Wl,--no-undefined,-Bsymbolic',
        '-lquest2-v77-ahb','-ldl','-o',str(out/'libquest2-vulkan-wrapper.so')],check=True)
hashes={n:sha(out/n) for n in sorted(expected)}
(out/'SHA256SUMS').write_text(''.join(f'{h}  {n}\n' for n,h in hashes.items()))
print('CACHED_MEMORY_PRIVATE_CANDIDATE',hashes['libgsl.so'])
