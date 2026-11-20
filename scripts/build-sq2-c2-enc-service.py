#!/usr/bin/env python3
"""Build the SQ2 Codec2 AIDL hardware encoder service binary (sq2-c2-enc-service)."""
import argparse
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TOOLCHAINS = Path('/mnt/spx-workspace/toolchains')
DEFAULT_PLATFORM = Path('/mnt/spx-workspace/chromebookOS/out/hybrid-finish-platform-integrated-20261116-v2/platform')
DEFAULT_VENDOR_FOUNDATION = Path('/mnt/spx-workspace/chromebookOS/out/hybrid-finish-platform-20261116/files/vendor_a/lib64/libstagefright_foundation.so')


def generate_aidl_headers(aidl_bin, hw_if, native_ref, out_dir):
    """Generate NDK AIDL C++ headers for media.c2, bufferpool2, common, and graphics."""
    inc_dir = out_dir / 'aidl-gen/include'
    src_dir = out_dir / 'aidl-gen/src'
    extra_dir = out_dir / 'aidl-extra/android/hardware'
    inc_dir.mkdir(parents=True, exist_ok=True)
    src_dir.mkdir(parents=True, exist_ok=True)
    extra_dir.mkdir(parents=True, exist_ok=True)
    (extra_dir / 'HardwareBuffer.aidl').write_text(
        'package android.hardware;\n'
        '@JavaOnlyStableParcelable @NdkOnlyStableParcelable @RustOnlyStableParcelable @VintfStability\n'
        'parcelable HardwareBuffer ndk_header "android/hardware_buffer_aidl.h";\n',
        encoding='utf-8',
    )

    c2_srcs = sorted((hw_if / 'media/c2/aidl/android/hardware/media/c2').glob('*.aidl'))
    bp_srcs = sorted((hw_if / 'media/bufferpool/aidl/android/hardware/media/bufferpool2').glob('*.aidl'))
    cm_srcs = sorted((hw_if / 'common/aidl/android/hardware/common').glob('*.aidl'))
    gr_srcs = sorted((hw_if / 'graphics/common/aidl/android/hardware/graphics/common').glob('*.aidl'))
    fmq_srcs = sorted((hw_if / 'common/fmq/aidl/android/hardware/common/fmq').glob('*.aidl'))

    subprocess.run([
        str(aidl_bin), '--lang=ndk', '--stability=vintf', '--structured',
        '-I', str(out_dir / 'aidl-extra'),
        '-I', str(native_ref / 'aidl/gui'),
        '-I', str(native_ref / 'aidl/binder'),
        '-I', str(hw_if / 'media/c2/aidl'),
        '-I', str(hw_if / 'media/bufferpool/aidl'),
        '-I', str(hw_if / 'graphics/common/aidl'),
        '-I', str(hw_if / 'common/aidl'),
        '-I', str(hw_if / 'common/fmq/aidl'),
        '-h', str(inc_dir), '-o', str(src_dir),
        *[str(p) for p in (*c2_srcs, *bp_srcs, *cm_srcs, *gr_srcs)],
    ], check=True)

    subprocess.run([
        str(aidl_bin), '--lang=ndk', '--stability=vintf', '--structured',
        '-I', str(hw_if / 'common/fmq/aidl'),
        '-I', str(hw_if / 'common/aidl'),
        '-h', str(inc_dir), '-o', str(src_dir),
        *[str(p) for p in fmq_srcs],
    ], check=True)
    return inc_dir


def build_service(c2_deps, output, toolchains=DEFAULT_TOOLCHAINS,
                  platform=DEFAULT_PLATFORM, vendor_foundation=DEFAULT_VENDOR_FOUNDATION):
    """Compile SimpleC2Interface + sq2-c2-enc-aidl-service into an AArch64 vendor binary."""
    c2_deps = Path(c2_deps).resolve()
    output = Path(output).resolve()
    work = output.parent
    work.mkdir(parents=True, exist_ok=True)

    ndk_cxx = toolchains / 'sq2-graphics/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang++'
    aidl_bin = toolchains / 'sq2-graphics/sdk/android-15/aidl'
    hw_if = toolchains / 'sq2-c2-hardware-interfaces'
    native_ref = toolchains / 'sq2-c2-native-reference'
    plat_lib64 = platform / 'lib64'
    hidl_inc = platform / 'toolchains/sq2-quest2-hidl/include'
    plat_hdr = platform / 'headers'

    aidl_inc = generate_aidl_headers(aidl_bin, hw_if, native_ref, work)

    common_flags = [
        str(ndk_cxx), '-std=c++17', '-fno-rtti', '-nostdinc++', '-nostdlib++',
        '-O2', '-Wall', '-Wextra', '-Wno-deprecated-copy',
        '-isystem', str(c2_deps / 'libcxx'),
        '-I', str(aidl_inc),
        '-I', str(c2_deps / 'av/media/codec2/hal/aidl/include'),
        '-I', str(c2_deps / 'av/media/codec2/hal/common/include'),
        '-I', str(c2_deps / 'av/media/codec2/core/include'),
        '-I', str(c2_deps / 'av/media/codec2/vndk/include'),
        '-I', str(c2_deps / 'av/media/codec2/components/base/include'),
        '-I', str(c2_deps / 'av/media/module/foundation/include'),
        '-I', str(c2_deps / 'libbase/include'),
        '-I', str(c2_deps / 'libhardware'),
        '-I', str(hidl_inc),
        '-I', str(plat_hdr),
        '-I', str(native_ref / 'libs/binder/ndk/include_platform'),
        '-I', str(native_ref / 'libs/binder/ndk/include_cpp'),
        '-I', str(native_ref / 'libs/nativewindow/include'),
    ]

    simple_obj = work / 'SimpleC2Interface.o'
    subprocess.run([
        *common_flags, '-c',
        str(c2_deps / 'av/media/codec2/components/base/SimpleC2Interface.cpp'),
        '-o', str(simple_obj),
    ], check=True)

    subprocess.run([
        *common_flags,
        str(ROOT / 'probe/video/sq2-c2-enc-aidl-service.cpp'),
        str(simple_obj),
        str(plat_lib64 / 'libcodec2_aidl.so'),
        str(plat_lib64 / 'android.hardware.media.c2-V2-ndk.so'),
        str(plat_lib64 / 'libbinder_ndk.so'),
        str(plat_lib64 / 'libcodec2_vndk.so'),
        str(plat_lib64 / 'libcodec2.so'),
        str(vendor_foundation),
        str(plat_lib64 / 'libbase.so'),
        str(plat_lib64 / 'libcutils.so'),
        str(plat_lib64 / 'libutils.so'),
        str(plat_lib64 / 'libc++.so'),
        '-Wl,--allow-shlib-undefined', '-llog',
        '-o', str(output),
    ], check=True)
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--c2-deps', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--toolchains', type=Path, default=DEFAULT_TOOLCHAINS)
    parser.add_argument('--platform', type=Path, default=DEFAULT_PLATFORM)
    parser.add_argument('--vendor-foundation', type=Path, default=DEFAULT_VENDOR_FOUNDATION)
    args = parser.parse_args()
    out = build_service(
        args.c2_deps, args.output,
        toolchains=args.toolchains, platform=args.platform,
        vendor_foundation=args.vendor_foundation,
    )
    print(f'BUILT_SQ2_C2_ENC_SERVICE={out} bytes={os.path.getsize(out)}')


if __name__ == '__main__':
    main()
