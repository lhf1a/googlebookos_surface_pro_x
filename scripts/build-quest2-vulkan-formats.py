#!/usr/bin/env python3
"""Private color-format candidate on the pinned, tested runtime-v5 base."""
import hashlib
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess

root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--bgra-export',action='store_true')
parser.add_argument('--boot-runtime',action='store_true',help='v8: v7 with successful per-call statistics disabled')
args=parser.parse_args()
if args.boot_runtime:args.bgra_export=True
spec=importlib.util.spec_from_file_location('base',root/'scripts/prepare-quest2-vulkan-only.py')
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
expected=dict(base.EXPECTED)
expected.update({
 'libgsl.so':'b302ae0d5a6041be816ebe07c0c8c2686baf231b54182ee319731512db35523d',
 'libquest2-v77-ahb.so':'3f758905767cb150aea77dee68c300b55c39701394edbf9a899ad770511ba3f2',
 'libquest2-vulkan-wrapper.so':'4cfe5cba0496e9b007344a53725aa2595cdfa0d777f880f9d468a065b36c78a0',
 'vulkan-v77-bridge.so':'c6253f16ae790a5f845c563e2705e36a005aecf327ed312f7de4ddcbd0e60dce',
 'qgl_config.txt':'1f93a7a1446b2e934f12f23aa55514ee41852bf6df7851788d053cbd90e1f008'})
source=root/'out/quest2-gpu/vulkan-runtime-v5'
out=root/('out/quest2-gpu/vulkan-runtime-v7' if args.bgra_export else 'out/quest2-gpu/vulkan-runtime-v6')
if args.boot_runtime:out=root/'out/quest2-gpu/vulkan-runtime-v8'
if out.exists():raise SystemExit('Refusing existing output')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for name,digest in expected.items():
 if (source/name).is_symlink() or sha(source/name)!=digest:raise SystemExit('Input mismatch: '+name)
platform=root/'out/quest2-gpu/platform-link'
for name,digest in {'libc++.so':'cb118e98c74d3454858921123b9b51ba13df9cad7dfa141dd892c453661a4d78',
                     'libhardware.so':'89869693c2bbb28c49d91b05110698be757d243a6d6b929488abb3f804c368de'}.items():
 if (platform/name).is_symlink() or sha(platform/name)!=digest:raise SystemExit('Platform mismatch: '+name)
work=Path(os.environ.get('SQ2_GRAPHICS_WORK','/root/.cache/sq2-graphics'))
cc=work/'android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang'
out.mkdir()
for name in expected:shutil.copy2(source/name,out/name)
flags=['-O2','-fPIC','-shared','-Wall','-Wextra','-Werror','-DSQ2_VK_AHB_ALLOCATE',
       '-DSQ2_VK_CACHED_MEMORY','-DSQ2_VK_COLOR_FORMATS',
       '-I'+str(work/'src/mesa-26.2.4/include/android_stub')]
if args.bgra_export:flags+=['-DSQ2_VK_BGRA_EXPORT']
if args.boot_runtime:flags+=['-DSQ2_VK_QUIET_RUNTIME']
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
(out/'candidate.json').write_text(json.dumps({'sha256':hashes,'base':'runtime-v5',
 'actual_vulkan_version':'1.1.295','required_feature_version':4198400,'mesa_egl_preserved':True,
 'cts_passed':False,'boot_validated':False,'experimental_color_formats':True,
 'experimental_bgra_export':args.bgra_export,'quiet_success_statistics':args.boot_runtime},indent=2)+'\n')
print('VULKAN_COLOR_CANDIDATE_BUILT',hashes['libquest2-vulkan-wrapper.so'])
