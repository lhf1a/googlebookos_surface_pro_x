from pathlib import Path
import subprocess
r=Path(__file__).resolve().parents[1];d=r/'out/video-decode/codec2-integration';o=d/'aidl-store';ndk=Path('/root/.cache/sq2-graphics/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64');av=Path('/root/.cache/sq2-codec2-av-reference/media/codec2')
o.mkdir(parents=True,exist_ok=True)
cmd=[str(ndk/'bin/aarch64-linux-android35-clang++'),'-std=c++17','-fno-rtti','-nostdinc++','-nostdlib++','-isystem','/root/.cache/sq2-c2-platform-libcxx-headers']
for p in [Path('/root/.cache/sq2-codec2-av-reference/media/module/foundation/include'),av/'hal/aidl/include',av/'hal/common/include',Path('/root/.cache/sq2-c2-aidl-generated/include'),Path('/root/.cache/sq2-c2-native-reference/libs/binder/ndk/include_platform'),Path('/root/.cache/sq2-c2-native-reference/libs/binder/ndk/include_cpp'),av/'core/include',av/'vndk/include',av/'components/base/include',Path('/root/.cache/sq2-quest2-hidl/include'),Path('/root/.cache/sq2-graphics/src/mesa-26.2.4/include/android_stub')]:cmd+=['-I',str(p)]
cmd += ['-I',str(r/'out/video-decode/h264-vector'),str(r/'probe/video/sq2-c2-aidl-service.cpp'),str(d/'libcodec2_soft_common.so'),str(d/'libcodec2_aidl_noisurface.so'),str(d/'android.hardware.media.c2-V2-ndk.so'),str(d/'libbinder_ndk.so'),str(d/'libcodec2_vndk.so'),str(d/'libcodec2.so'),str(d/'libc++.so'),'-Wl,--allow-shlib-undefined','-o',str(o/'interface-test')]
p=subprocess.run(cmd,capture_output=True,text=True);(o/'build.txt').write_text(repr(cmd)+'\n'+p.stdout+p.stderr+'\nexit='+str(p.returncode));print(p.stdout,p.stderr);raise SystemExit(p.returncode)
