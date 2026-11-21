"""Rebuild and update SQ2 Radio HAL binary and framework overlay during image assembly."""
import argparse
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
RADIO_OVERLAY_SRC = ROOT / 'probe/lte/overlay'
RADIO_OVERLAY_TARGET = 'etc/sq2-radio/sq2-radio-overlay.apk'
RADIO_BIN_TARGET = 'etc/sq2-radio/sq2-radio'


def _find_cache() -> Path:
    env_cache = os.environ.get('SPX_CACHE_ROOT')
    if env_cache:
        return Path(env_cache)
    for cand in (Path('/mnt/spx-workspace/toolchains'), Path('/root/.cache')):
        if cand.is_dir():
            return cand
    return Path('/root/.cache')


def _find_sdk() -> Path:
    env_sdk = os.environ.get('SQ2_ANDROID_SDK')
    if env_sdk:
        return Path(env_sdk)
    cache = _find_cache()
    for cand in (
        cache / 'sq2-graphics/sdk',
        Path('/mnt/spx-workspace/toolchains/sq2-graphics/sdk'),
        Path('/root/.cache/sq2-graphics/sdk'),
    ):
        if (cand / 'android-35/android.jar').is_file() and (cand / 'android-15/aapt2').is_file():
            return cand
    return cache / 'sq2-graphics/sdk'


def _find_binder_lib(private: Path | None = None) -> Path | None:
    env_lib = os.environ.get('SQ2_BINDER_LIB')
    candidates = []
    if env_lib:
        candidates.append(Path(env_lib))
    if private is not None:
        candidates.append(Path(private) / 'lte/libbinder_ndk.so')
    candidates.extend([
        ROOT / 'private/build-inputs/lte/libbinder_ndk.so',
        ROOT / 'out/lte-radio-20261103/libbinder_ndk.so',
        Path('/mnt/spx-workspace/chromebookOS/private/build-inputs/lte/libbinder_ndk.so'),
        Path('/mnt/spx-workspace/chromebookOS/out/hybrid-finish-platform-integrated-20261116-v2/platform/lib64/libbinder_ndk.so'),
    ])
    for cand in candidates:
        if cand.is_file():
            return cand
    return None


def _verify_arm64_elf(data: bytes, label: str) -> None:
    if len(data) < 64 or data[:4] != b'\x7fELF' or data[4] != 2 or data[5] != 1:
        raise ValueError(f'{label}: not an ELF64 little-endian binary')
    e_machine = struct.unpack_from('<H', data, 18)[0]
    if e_machine != 183:
        raise ValueError(f'{label}: expected EM_AARCH64 (183), got {e_machine}')


def build_radio_overlay(output: Path) -> bytes:
    sdk = _find_sdk()
    platform, tools = sdk / 'android-35/android.jar', sdk / 'android-15'
    key = Path(os.environ.get('SPX_RADIO_OVERLAY_KEYSTORE',
                              str(ROOT / 'private/sq2-radio/local.keystore')))
    key.parent.mkdir(parents=True, exist_ok=True)
    if not key.exists():
        subprocess.run([
            'keytool', '-genkeypair', '-keystore', str(key),
            '-storepass', 'android', '-keypass', 'android', '-alias', 'sq2-radio',
            '-keyalg', 'RSA', '-keysize', '2048', '-validity', '3650',
            '-dname', 'CN=Local Radio Overlay',
        ], check=True, capture_output=True)
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='sq2-radio-overlay-') as tmp:
        work = Path(tmp)
        res = work / 'res'
        (res / 'values').mkdir(parents=True)
        (res / 'xml').mkdir(parents=True)
        shutil.copyfile(RADIO_OVERLAY_SRC / 'res/values/config.xml', res / 'values/config.xml')
        shutil.copyfile(RADIO_OVERLAY_SRC / 'res/xml/apns.xml', res / 'xml/apns.xml')
        res_zip = work / 'overlay-res.zip'
        subprocess.run([str(tools / 'aapt2'), 'compile', '--dir', str(res),
                        '-o', str(res_zip)], check=True)
        subprocess.run([str(tools / 'aapt2'), 'link',
                        '--manifest', str(RADIO_OVERLAY_SRC / 'AndroidManifest.xml'),
                        '-I', str(platform), '--auto-add-overlay',
                        '-o', str(work / 'unsigned.apk'), str(res_zip)], check=True)
        subprocess.run([str(tools / 'zipalign'), '-f', '-P', '16', '4',
                        str(work / 'unsigned.apk'), str(work / 'aligned.apk')], check=True)
        subprocess.run([str(tools / 'apksigner'), 'sign', '--ks', str(key),
                        '--ks-pass', 'pass:android', '--key-pass', 'pass:android',
                        '--out', str(output), str(work / 'aligned.apk')], check=True)
        subprocess.run([str(tools / 'apksigner'), 'verify', str(output)], check=True)
    return output.read_bytes()


def build_radio_binary(private: Path | None = None) -> bytes:
    cache = _find_cache()
    binder_lib = _find_binder_lib(private)
    if binder_lib is None:
        raise FileNotFoundError('Missing libbinder_ndk.so for sq2-radio')
    out = ROOT / 'out/lte-radio-20261103'
    out.mkdir(parents=True, exist_ok=True)
    dst_binder = out / 'libbinder_ndk.so'
    if not dst_binder.exists() or dst_binder.resolve() != binder_lib.resolve():
        shutil.copyfile(binder_lib, dst_binder)
    env = os.environ.copy()
    env['SPX_CACHE_ROOT'] = str(cache)
    subprocess.run([sys.executable, str(ROOT / 'scripts/build-sq2-radio.py')],
                   env=env, check=True)
    data = (out / 'sq2-radio').read_bytes()
    _verify_arm64_elf(data, RADIO_BIN_TARGET)
    return data


def radio_toolchains_available(private: Path | None = None) -> bool:
    cache = _find_cache()
    return (
        (cache / 'sq2-c2-hardware-interfaces/.git').exists()
        and (cache / 'sq2-c2-native-reference/libs/binder/ndk/include_platform').is_dir()
        and (cache / 'sq2-graphics/sdk/android-15/aidl').is_file()
        and (cache / 'sq2-graphics/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang++').is_file()
        and _find_binder_lib(private) is not None
    )


def install(records, bodies, new_record, *, private: Path | None = None,
            radio_bin: bytes | None = None, radio_overlay: bytes | None = None) -> dict:
    updated = {}
    if RADIO_OVERLAY_TARGET in bodies:
        if radio_overlay is None:
            sdk = _find_sdk()
            if (sdk / 'android-35/android.jar').is_file() and (sdk / 'android-15/aapt2').is_file():
                with tempfile.TemporaryDirectory(prefix='sq2-radio-overlay-') as tmp:
                    radio_overlay = build_radio_overlay(Path(tmp) / 'sq2-radio-overlay.apk')
        if radio_overlay is not None:
            bodies[RADIO_OVERLAY_TARGET] = radio_overlay
            records[RADIO_OVERLAY_TARGET] = new_record(0o100600)
            updated['radio_overlay'] = RADIO_OVERLAY_TARGET

    if RADIO_BIN_TARGET in bodies:
        if radio_bin is None and radio_toolchains_available(private):
            radio_bin = build_radio_binary(private)
        if radio_bin is not None:
            _verify_arm64_elf(radio_bin, RADIO_BIN_TARGET)
            bodies[RADIO_BIN_TARGET] = radio_bin
            records[RADIO_BIN_TARGET] = new_record(0o100600)
            updated['radio_bin'] = RADIO_BIN_TARGET

    return updated


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    out_dir = args.output_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    apk = build_radio_overlay(out_dir / 'sq2-radio-overlay.apk')
    radio = build_radio_binary()
    print(f'RADIO_BUILD_OK apk={len(apk)} bin={len(radio)}')
