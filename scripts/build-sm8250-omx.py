#!/usr/bin/env python3
"""Build pinned QTI OMX source with the current Android platform ABI; never deploy."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ['mm-video-v4l2/vidc/vdec/src/omx_vdec_v4l2.cpp',
           'mm-video-v4l2/vidc/vdec/src/omx_vdec_v4l2_params.cpp',
           'mm-video-v4l2/vidc/vdec/src/ts_parser.cpp',
           'mm-video-v4l2/vidc/common/src/vidc_common.cpp',
           'mm-video-v4l2/vidc/common/src/vidc_debug.cpp',
           'mm-video-v4l2/vidc/common/src/vidc_vendor_extensions.cpp']


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(__doc__)
    for name in ('prepared', 'toolchains', 'full-stack', 'renderer', 'omx-dir', 'private-libs', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--hdr-sequence-reset', action='store_true')
    args = parser.parse_args()
    prepared, cache, stack, renderer, omx, libs, out = [getattr(args, n).resolve() for n in
        ('prepared', 'toolchains', 'full_stack', 'renderer', 'omx_dir', 'private_libs', 'output')]
    recipe = json.loads((ROOT / 'configs/sm8250-omx-source.json').read_text())
    for name, expected in recipe['prepared_sources'].items():
        if sha(prepared / name) != expected:
            raise ValueError('Source mismatch: ' + name)
    for name, expected in recipe['private_dependencies'].items():
        if sha(libs / name) != expected:
            raise ValueError('Dependency mismatch: ' + name)
    out.mkdir(parents=True, exist_ok=False)
    media = out / 'media'
    shutil.copytree(prepared / 'media', media)
    if args.hdr_sequence_reset:
        subprocess.run(['patch', '-p1', '--batch', '--fuzz=0', '-i',
                        str(ROOT / 'patches/sm8250-omx-hdr-sequence.patch')], cwd=media, check=True)
    includes = [prepared / 'headers', media / 'mm-video-v4l2/vidc/vdec/inc',
        media / 'mm-video-v4l2/vidc/common/inc', media / 'mm-core/inc',
        media / 'libplatformconfig', media / 'libc2dcolorconvert',
        stack / 'mapper5/include', stack / 'mapper5',
        stack / 'sm8350-reference-v1/android_vendor_qcom_opensource_display-commonsys-intf/gralloc',
        stack / 'sm8350-reference-v1/android_vendor_qcom_opensource_display-commonsys-intf/include',
        stack / 'sm8350-composer-deps-v1/hardware-include',
        stack / 'sm8350-composer-deps-v1/libs-nativebase-include',
        cache / 'sq2-c2-core-reference/libutils/include', cache / 'sq2-c2-core-reference/libcutils/include',
        cache / 'sq2-c2-core-reference/libsystem/include', cache / 'sq2-c2-logging-reference/liblog/include',
        cache / 'sq2-c2-base-reference/include', renderer / 'native/headers/media_plugin',
        cache / 'sq2-c2-native-reference/libs/nativewindow/include']
    cc = cache / 'sq2-graphics/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang++'
    flags = ['-include', str(prepared / 'headers/ion-compat.h'), '-std=c++17', '-O2', '-g',
        '-fPIC', '-fno-rtti', '-fno-exceptions', '-nostdinc++', '-nostdlib++',
        '-isystem', str(cache / 'sq2-c2-platform-libcxx-headers'),
        '-D__alignx(x)=__attribute__((__aligned__(x)))', '-D__align=__alignx', '-Dinline=__inline',
        '-DIMAGE_APPS_PROC', '-D_ANDROID_', '-DCDECL', '-DT_ARM', '-DNO_ARM_CLZ',
        '-DENABLE_DEBUG_ERROR', '-D_ANDROID_ICS_', '-DPROCESS_EXTRADATA_IN_OUTPUT_PORT',
        '-D_UBWC_', '-DUSE_ION', '-D_ANDROID_O_MR1_DIVX_CHANGES', '-DADAPTIVE_PLAYBACK_SUPPORTED',
        '-DFLEXYUV_SUPPORTED', '-DALLOCATE_OUTPUT_NATIVEHANDLE', '-Werror', '-Wno-enum-compare',
        '-Wno-deprecated-declarations', '-Wno-parentheses', '-Wno-c99-designator', '-Wno-vla-cxx-extension']
    for include in includes:
        flags += ['-I', str(include)]
    record = {'source_commit': recipe['commit'], 'hdr_sequence_reset': args.hdr_sequence_reset,
              'commands': [], 'inputs': {}, 'scope': 'candidate only, no deployment or Main10 advertisement'}

    def run(name, command):
        command = [str(x) for x in command]
        result = subprocess.run(command, capture_output=True, text=True)
        (out / (name + '.log')).write_text(result.stdout + result.stderr)
        record['commands'].append({'argv': command, 'exit': result.returncode})
        (out / 'BUILD.json').write_text(json.dumps(record, indent=2) + '\n')
        if result.returncode:
            raise RuntimeError('Build failed: ' + str(out / (name + '.log')))

    objects = []
    for name in SOURCES:
        src = media / name
        obj = out / (src.stem + '.o')
        run(src.stem, [cc, *flags, '-MD', '-MF', obj.with_suffix('.d'), '-c', src, '-o', obj])
        objects.append(obj)
    so = out / 'libOmxVdec.so'
    run('link', [cc, '-shared', '-nostdlib++', '-Wl,-z,defs,-z,relro,-z,now,-z,noexecstack',
        '-Wl,-soname,libOmxVdec.so', *objects, '-L', libs, '-L', omx / 'platform',
        '-llog', '-lcutils', '-ldl', '-lion', '-lc2dcolorconvert', '-lqdMetaData',
        '-lplatformconfig', '-lnativewindow', '-lc++', '-o', so])
    for depfile in out.glob('*.d'):
        # Prepared paths must not contain spaces: the existing clang recipes use make depfiles.
        text = depfile.read_text().replace('\\\n', '')
        for name in text.split(':', 1)[1].split():
            dep = Path(name)
            record['inputs'][str(dep)] = sha(dep)
    for path in [cc, *libs.glob('*.so'), ROOT / 'patches/sm8250-omx-hdr-sequence.patch']:
        record['inputs'][str(path)] = sha(path)
    data = so.read_bytes()
    if data[:6] != b'\x7fELF\x02\x01' or int.from_bytes(data[18:20], 'little') != 183:
        raise ValueError('Not ARM64 shared ELF')
    record['sha256'] = sha(so)
    (out / 'BUILD.json').write_text(json.dumps(record, indent=2) + '\n')
    print(record['sha256'])


if __name__ == '__main__':
    main()
