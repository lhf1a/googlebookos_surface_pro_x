#!/usr/bin/env python3
"""Build the experimental SPX native camera provider against AOSP/NDK headers."""
import argparse
from pathlib import Path
import subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('dependencies',type=Path,help='directory with jpeg, system-media, native-headers')
p.add_argument('aidl',type=Path,help='build-spx-camera-aidl.py output')
p.add_argument('libraries',type=Path,help='matching device C libraries')
p.add_argument('toolchains',type=Path)
p.add_argument('output',type=Path)
p.add_argument('--platform-headers', type=Path, required=True)
p.add_argument('--binder-library', type=Path, required=True)
a=p.parse_args()
root=Path(__file__).resolve().parents[1]
tc=a.toolchains.resolve()
ndk=tc/'sq2-graphics/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64'
includes=[a.platform_headers.resolve(),
          tc/'sq2-graphics/src/mesa-26.2.4/include/android_stub',
          a.aidl.resolve()/'include',a.dependencies.resolve()/'system-media',
          a.dependencies.resolve()/'jpeg',a.dependencies.resolve()/'native-headers/libs/nativewindow/include',
          tc/'sq2-c2-core-reference/libcutils/include',
          tc/'sq2-c2-native-reference/libs/binder/ndk/include_cpp',
          tc/'sq2-c2-native-reference/libs/binder/ndk/include_platform']
sources=['spx-capture.cpp','spx-isp.cpp','spx-camera-metadata.cpp','spx-camera-provider.cpp']
cmd=[str(ndk/'bin/aarch64-linux-android35-clang++'),'-std=c++20','-O2',
     '-Wall','-Wextra','-Werror','-static-libstdc++']
cmd+=['-I'+str(d) for d in includes]
cmd += [str(root/'probe/camera'/f) for f in sources]
cmd += [str(a.aidl.resolve()/'libspx-camera-aidl.a')]
cmd += [str(a.libraries.resolve()/(f+'.so')) for f in ['libcamera_metadata','libnativewindow','libjpeg','libcutils']]
cmd += [str(a.binder_library.resolve()),
        '-llog','-ldl','-Wl,--allow-shlib-undefined','-o',str(a.output.resolve())]
subprocess.run(cmd,check=True)
