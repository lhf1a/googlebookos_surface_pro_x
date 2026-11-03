#!/usr/bin/env python3
"""Build the SQ2 data-only Radio AIDL adapter (no deployment)."""
import argparse
import concurrent.futures
import hashlib
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
CACHE = Path(os.environ.get('SPX_CACHE_ROOT', '/root/.cache'))
REVISION = '1a56e38edc2f2f6189ef405ee1edce554e15cbc0'
PARTS = ('', '.config', '.data', '.modem', '.network', '.sim', '.voice', '.messaging')


def prepare(out):
    reference = out / 'reference'
    repo = CACHE / 'sq2-c2-hardware-interfaces'
    if subprocess.check_output(['git', '-C', str(repo), 'rev-parse', 'HEAD'], text=True).strip() != REVISION:
        raise RuntimeError('Radio reference revision mismatch')
    if not reference.exists():
        reference.mkdir(parents=True)
        archive = subprocess.Popen(['git', '-C', str(repo), 'archive', REVISION, 'radio/aidl', 'common/aidl'], stdout=subprocess.PIPE)
        subprocess.run(['tar', '-xf', '-', '-C', str(reference)], stdin=archive.stdout, check=True)
        archive.stdout.close()
        if archive.wait():
            raise RuntimeError('Reference extraction failed')
    api = reference / 'radio/aidl/aidl_api'
    dirs = [api / ('android.hardware.radio' + p) / '3' for p in PARTS]
    dirs += [reference / 'common/aidl/aidl_api/android.hardware.common/2']
    aidl = CACHE / 'sq2-graphics/sdk/android-15/aidl'
    generated = out / 'generated'
    stamp = generated / 'revision'
    if stamp.exists() and stamp.read_text() == REVISION:
        subprocess.run(['python3', str(ROOT / 'scripts/generate-sq2-radio-defaults.py')], check=True)
        return generated
    for directory in dirs:
        for src in sorted(directory.rglob('*.aidl')):
            subprocess.run([str(aidl), '--lang=ndk', '--structured', '--stability=vintf',
                            '--version=' + directory.name, '--hash=' + (directory / '.hash').read_text().splitlines()[-1],
                            '--out=' + str(generated / 'src'), '--header_out=' + str(generated / 'include')]
                           + ['-I' + str(d) for d in dirs] + [str(src)], check=True, capture_output=True)
    print('Generated', len(list(generated.rglob('*.cpp'))), 'Radio V3 translation units', flush=True)
    stamp.write_text(REVISION)
    subprocess.run(['python3', str(ROOT / 'scripts/generate-sq2-radio-defaults.py')], check=True)
    return generated


def build(out, generated):
    ndk = CACHE / 'sq2-graphics/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64'
    cmd = [str(ndk / 'bin/aarch64-linux-android35-clang++'), '-std=c++17', '-O2', '-fPIC', '-pthread',
           '-I' + str(generated / 'include'),
           '-I' + str(CACHE / 'sq2-c2-native-reference/libs/binder/ndk/include_platform'),
           '-I' + str(CACHE / 'sq2-c2-native-reference/libs/binder/ndk/include_cpp'),
           '-I' + str(ROOT / 'probe/lte')]
    objects = out / 'obj'
    objects.mkdir(exist_ok=True)
    sources = sorted((generated / 'src').rglob('*.cpp')) + [ROOT / 'probe/lte/sq2-radio.cpp']
    def compile_one(src):
        obj = objects / (hashlib.sha256(str(src).encode()).hexdigest()[:16] + '.o')
        if not obj.exists() or obj.stat().st_mtime < src.stat().st_mtime or src.name == 'sq2-radio.cpp':
            p = subprocess.run(cmd + ['-c', str(src), '-o', str(obj)], capture_output=True, text=True)
            if p.returncode:
                raise RuntimeError(str(src) + '\n' + p.stderr)
        return obj
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
        objs = list(executor.map(compile_one, sources))
    binder = out / 'libbinder_ndk.so'
    if not binder.exists():
        raise RuntimeError('Provide the target system libbinder_ndk.so for platform service symbols')
    link = subprocess.run(cmd + [str(o) for o in objs] + ['-static-libstdc++', str(binder), '-llog',
                         '-Wl,--allow-shlib-undefined', '-o', str(out / 'sq2-radio')], capture_output=True, text=True)
    if link.returncode:
        raise RuntimeError(link.stderr)
    print('Built', out / 'sq2-radio')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--prepare-only', action='store_true')
    args = parser.parse_args()
    output = ROOT / 'out/lte-radio-20261103'
    output.mkdir(parents=True, exist_ok=True)
    gen = prepare(output)
    if not args.prepare_only:
        build(output, gen)
