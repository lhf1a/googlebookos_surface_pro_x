#!/usr/bin/env python3
"""Build the Surface sensor sub-HAL against AOSP HIDL and matching device libraries."""
import argparse,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for name in ('interfaces','toolchains','libraries','output'):p.add_argument(name,type=Path)
a=p.parse_args()
root=Path(__file__).resolve().parents[1]
interfaces,tc,libs,out=(getattr(a,n).resolve() for n in ('interfaces','toolchains','libraries','output'))
out.mkdir(parents=True,exist_ok=False)
hidl=tc/'sq2-quest2-hidl'
ndk=tc/'sq2-graphics/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64'
env=dict(os.environ,LD_LIBRARY_PATH=str(hidl))
for version in ('1.0','2.0','2.1'):
 subprocess.run([str(hidl/'hidl-gen'),'-o',str(out/'generated'),'-Lc++-headers',
   '-randroid.hardware:'+str(interfaces),'-randroid.hidl:'+str(hidl/'hal/android/hidl'),
   'android.hardware.sensors@'+version],env=env,check=True)
flags=[str(ndk/'bin/aarch64-linux-android35-clang++'),'-std=c++17','-O2','-fPIC','-fno-rtti',
 '-nostdlib++','-Wall','-Wextra','-Werror','-include',str(root/'probe/graphics/quest2-platform-cxx.h')]
for inc in (out/'generated',hidl/'generated',hidl/'include',interfaces/'sensors/common/default/2.X/multihal/include'):
 flags+=['-isystem',str(inc)]
link=['-L'+str(libs),'-Wl,--no-undefined']+['-l:'+n+'.so' for n in (
 'android.hardware.sensors@2.1','android.hardware.sensors@2.0','android.hardware.sensors@1.0',
 'android.hardware.sensors@2.0-ScopedWakelock','libhidlbase','libutils','libcutils','libc++','liblog')]
src=[str(root/'probe/sensors'/n) for n in ('spx-sensors-subhal.cpp','spx-ssc.cpp','spx-lux.cpp')]
subprocess.run(flags+['-shared']+src+link+['-o',str(out/'sensors.spx.so')],check=True)
subprocess.run(flags+[str(root/'tests/spx_subhal_test.cpp')]+link+['-ldl','-o',str(out/'subhal-test')],check=True)
