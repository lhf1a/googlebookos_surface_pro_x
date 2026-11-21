"""Build and install non-Pixel/non-CrOS-EC runtime hygiene overlays and stubs.

Provides:
- overlay/spx-systemui-overlay.apk (in etc/sq2-qti-system/root/): disables Pixel
  IGoogleBattery warning probes in SystemUI on non-Pixel hardware.
- etc/spx-hygiene/spx-ec-executor-stub: minimal no-op vendor.google.desktop.IEcExecutor/default
  AIDL stub staged by spx-service-hygiene-stage when /dev/cros_ec is absent.
"""
import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SYSUI_OVERLAY_SRC = ROOT / 'probe/power/systemui-overlay'
SYSUI_OVERLAY_REL = 'overlay/spx-systemui-overlay.apk'
EC_STUB_SRC = ROOT / 'probe/power/spx-ec-executor-stub.cpp'
EC_STUB_TARGET = 'etc/spx-hygiene/spx-ec-executor-stub'


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


def _find_ndk() -> Path:
    cache = _find_cache()
    for cand in (
        cache / 'sq2-graphics/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64',
        Path('/mnt/spx-workspace/toolchains/sq2-graphics/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64'),
        Path('/root/.cache/sq2-graphics/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64'),
    ):
        if (cand / 'bin/aarch64-linux-android35-clang++').is_file():
            return cand
    return cache / 'sq2-graphics/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64'


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


def build_systemui_overlay(output: Path) -> bytes:
    sdk = _find_sdk()
    platform, tools = sdk / 'android-35/android.jar', sdk / 'android-15'
    key = Path(os.environ.get('SPX_SYSTEMUI_OVERLAY_KEYSTORE',
                              str(ROOT / 'private/systemui-overlay/signing.keystore')))
    key.parent.mkdir(parents=True, exist_ok=True)
    if not key.exists():
        subprocess.run([
            'keytool', '-genkeypair', '-keystore', str(key),
            '-storepass', 'android', '-keypass', 'android', '-alias', 'spx-systemui-overlay',
            '-keyalg', 'RSA', '-keysize', '2048', '-validity', '10000',
            '-dname', 'CN=Surface SystemUI Overlay',
        ], check=True, capture_output=True)
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='spx-sysui-overlay-') as tmp:
        work = Path(tmp)
        res_zip = work / 'resources.zip'
        subprocess.run([str(tools / 'aapt2'), 'compile', '--dir', str(SYSUI_OVERLAY_SRC / 'res'),
                        '-o', str(res_zip)], check=True)
        subprocess.run([str(tools / 'aapt2'), 'link',
                        '--manifest', str(SYSUI_OVERLAY_SRC / 'AndroidManifest.xml'),
                        '-I', str(platform), '--auto-add-overlay',
                        '-o', str(work / 'unsigned.apk'), str(res_zip)], check=True)
        subprocess.run([str(tools / 'zipalign'), '-f', '-P', '16', '4',
                        str(work / 'unsigned.apk'), str(work / 'aligned.apk')], check=True)
        subprocess.run([str(tools / 'apksigner'), 'sign', '--ks', str(key),
                        '--ks-pass', 'pass:android', '--key-pass', 'pass:android',
                        '--out', str(output), str(work / 'aligned.apk')], check=True)
        subprocess.run([str(tools / 'apksigner'), 'verify', str(output)], check=True)
    return output.read_bytes()


def build_ec_executor_stub(output: Path, private: Path | None = None) -> bytes:
    cache = _find_cache()
    ndk = _find_ndk()
    clangxx = ndk / 'bin/aarch64-linux-android35-clang++'
    binder_ref = cache / 'sq2-c2-native-reference/libs/binder/ndk'
    binder_lib = _find_binder_lib(private)
    if not clangxx.is_file():
        raise FileNotFoundError(f'Missing NDK clang++: {clangxx}')
    if not (binder_ref / 'include_platform').is_dir():
        raise FileNotFoundError(f'Missing binder platform headers: {binder_ref}')
    if binder_lib is None:
        raise FileNotFoundError('Missing libbinder_ndk.so for spx-ec-executor-stub')
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        str(clangxx), '-std=c++17', '-O2', '-fPIC', '-fno-exceptions', '-fno-rtti',
        '-nostdlib++', '-ffunction-sections', '-fdata-sections',
        '-Wall', '-Wextra', '-Werror',
        '-I' + str(binder_ref / 'include_platform'),
        '-I' + str(binder_ref / 'include_cpp'),
        str(EC_STUB_SRC),
        str(binder_lib), '-llog',
        '-Wl,--gc-sections', '-Wl,--allow-shlib-undefined',
        '-o', str(output),
    ]
    subprocess.run(cmd, check=True)
    data = output.read_bytes()
    _verify_arm64_elf(data, EC_STUB_TARGET)
    return data


def _ensure_parents(records, bodies, new_record, path: str) -> None:
    for parent in Path(path).parents:
        key = parent.as_posix()
        if key != '.' and key not in records:
            records[key] = new_record(0o040755)
            bodies[key] = b''


def install(records, bodies, new_record, *, private: Path | None = None,
            sysui_overlay: bytes | None = None, ec_stub: bytes | None = None) -> dict:
    installed = {}
    prefix = 'etc/sq2-qti-system/'
    listing = prefix + 'INSTALL_FILES'
    if listing in bodies:
        if sysui_overlay is None:
            sdk = _find_sdk()
            if (sdk / 'android-35/android.jar').is_file() and (sdk / 'android-15/aapt2').is_file():
                with tempfile.TemporaryDirectory(prefix='spx-sysui-overlay-') as tmp:
                    sysui_overlay = build_systemui_overlay(Path(tmp) / 'spx-systemui-overlay.apk')
        if sysui_overlay is not None:
            dst = prefix + 'root/' + SYSUI_OVERLAY_REL
            bodies[dst] = sysui_overlay
            records[dst] = new_record(0o100644)
            _ensure_parents(records, bodies, new_record, dst)
            lines = bodies[listing].splitlines()
            if SYSUI_OVERLAY_REL.encode() not in lines:
                bodies[listing] = bodies[listing].rstrip(b'\n') + b'\n' + SYSUI_OVERLAY_REL.encode() + b'\n'
            installed['sysui_overlay'] = dst

    if ec_stub is None:
        ndk = _find_ndk()
        cache = _find_cache()
        if (ndk / 'bin/aarch64-linux-android35-clang++').is_file() and \
           (cache / 'sq2-c2-native-reference/libs/binder/ndk/include_platform').is_dir() and \
           _find_binder_lib(private) is not None:
            with tempfile.TemporaryDirectory(prefix='spx-ec-stub-') as tmp:
                ec_stub = build_ec_executor_stub(Path(tmp) / 'spx-ec-executor-stub', private=private)
    if ec_stub is not None:
        _verify_arm64_elf(ec_stub, EC_STUB_TARGET)
        bodies[EC_STUB_TARGET] = ec_stub
        records[EC_STUB_TARGET] = new_record(0o100755)
        _ensure_parents(records, bodies, new_record, EC_STUB_TARGET)
        installed['ec_executor_stub'] = EC_STUB_TARGET

    return installed


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    out_dir = args.output_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    apk = build_systemui_overlay(out_dir / 'spx-systemui-overlay.apk')
    stub = build_ec_executor_stub(out_dir / 'spx-ec-executor-stub')
    print(f'HYGIENE_RUNTIME_OK apk={len(apk)} stub={len(stub)}')
