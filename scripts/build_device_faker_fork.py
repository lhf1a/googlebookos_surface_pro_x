#!/usr/bin/env python3
"""Fetch/build and verify the forked device_faker_gfx KernelSU module for baseline builds."""
import argparse
import hashlib
import io
import os
from pathlib import Path
import shutil
import struct
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
FORK_URL = 'https://github.com/lhf1a/device_faker_gfx.git'
FORK_BRANCH = 'main'
FORK_COMMIT = '4fca03096456a8ef05cf6faf5b412132c6439771'
DEFAULT_SRC_DIR = ROOT / 'out' / 'src' / 'device_faker_gfx'
DEFAULT_OUT_ZIP = ROOT / 'out' / 'device-faker' / 'device_faker-gfx.zip'
REQUIRED_ZIP_ENTRIES = {'module.prop', 'customize.sh', 'uninstall.sh', 'zygisk/arm64-v8a.so'}


def validate_device_faker_zip(data):
    """Verify device_faker ZIP contains required module metadata scripts and ELF64 AArch64 Zygisk lib."""
    if not data:
        raise ValueError('Empty device_faker zip')
    with zipfile.ZipFile(io.BytesIO(data), 'r') as zf:
        names = set(zf.namelist())
        missing = REQUIRED_ZIP_ENTRIES - names
        if missing:
            raise ValueError(f'device_faker zip missing required entries: {sorted(missing)}')
        prop_text = zf.read('module.prop').decode('utf-8', errors='replace')
        if 'id=device_faker' not in prop_text:
            raise ValueError('device_faker module.prop missing id=device_faker')
        so_bytes = zf.read('zygisk/arm64-v8a.so')
        if len(so_bytes) < 64 or so_bytes[:6] != b'\x7fELF\x02\x01':
            raise ValueError('device_faker zygisk/arm64-v8a.so is not ELF64 little-endian')
        machine = struct.unpack_from('<H', so_bytes, 18)[0]
        if machine != 183:
            raise ValueError(f'device_faker zygisk/arm64-v8a.so expected EM_AARCH64 (183), got {machine}')
    return hashlib.sha256(data).hexdigest()


def clone_or_update_fork(src_dir=DEFAULT_SRC_DIR, fork_url=FORK_URL, commit=FORK_COMMIT):
    """Clone or update the pinned device_faker_gfx fork into src_dir without touching home dirs."""
    src_dir = Path(src_dir)
    if (src_dir / '.git').is_dir():
        subprocess.run(['git', '-C', str(src_dir), 'fetch', '--depth', '1', fork_url, commit], check=True)
    else:
        src_dir.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(['git', 'clone', '--branch', FORK_BRANCH, fork_url, str(src_dir)], check=True)
    subprocess.run(['git', '-C', str(src_dir), 'checkout', '--detach', commit], check=True)
    return src_dir


def _discover_ndk_home(env):
    """Locate a readable Android NDK root from environment or standard cache locations."""
    for var in ('ANDROID_NDK_HOME', 'ANDROID_NDK_ROOT', 'NDK_HOME'):
        val = env.get(var)
        if val and (Path(val) / 'toolchains').is_dir():
            return val
    cc = env.get('SQ2_ANDROID_CC')
    if cc:
        for parent in Path(cc).resolve().parents:
            if (parent / 'toolchains').is_dir() and (parent / 'build').is_dir():
                return str(parent)
    cache_dir = Path.home() / '.cache'
    if cache_dir.is_dir():
        for cand in sorted(cache_dir.glob('ndk-*/android-ndk-*'), reverse=True):
            if (cand / 'toolchains').is_dir():
                return str(cand)
    return None


def build_from_source(source_dir, out_zip=DEFAULT_OUT_ZIP):
    """Build device_faker-(release).zip via upstream xtask and write validated out_zip."""
    source_dir = Path(source_dir)
    out_zip = Path(out_zip)
    if not (source_dir / 'Cargo.toml').is_file():
        raise FileNotFoundError(f'Not a device_faker source checkout: {source_dir}')
    env = dict(os.environ)
    cargo_bin = Path.home() / '.cargo' / 'bin'
    if cargo_bin.is_dir():
        env['PATH'] = f"{cargo_bin}:{env.get('PATH', '')}"
    ndk_home = _discover_ndk_home(env)
    if ndk_home:
        env.setdefault('ANDROID_NDK_HOME', ndk_home)
    subprocess.run(
        ['cargo', 'run', '--package', 'xtask', '--', 'build', '-r'],
        cwd=source_dir,
        env=env,
        check=True,
    )
    output_dir = source_dir / 'output'
    zips = sorted(output_dir.glob('device_faker*.zip'), key=lambda p: p.stat().st_mtime, reverse=True)
    if not zips:
        raise FileNotFoundError(f'No built device_faker zip found under {output_dir}')
    data = zips[0].read_bytes()
    validate_device_faker_zip(data)
    out_zip.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(zips[0], out_zip)
    return data


def resolve_or_build_device_faker_zip(
    private=None,
    explicit_zip=None,
    explicit_src=None,
    out_zip=DEFAULT_OUT_ZIP,
    allow_fetch=True,
):
    """Resolve forked device_faker KSU zip bytes from override, private input, cache, or fork source."""
    zip_path = explicit_zip or os.environ.get('SQ2_DEVICE_FAKER_ZIP')
    if zip_path:
        p = Path(zip_path)
        data = p.read_bytes()
        sha = validate_device_faker_zip(data)
        return data, {'source': 'explicit-zip', 'path': str(p), 'sha256': sha, 'fork_url': FORK_URL}

    if private is not None:
        priv_zip = Path(private) / 'ksu-modules' / 'device_faker.zip'
        if priv_zip.is_file():
            data = priv_zip.read_bytes()
            sha = validate_device_faker_zip(data)
            return data, {'source': 'private-ksu-modules', 'path': str(priv_zip), 'sha256': sha, 'fork_url': FORK_URL}

    src_override = explicit_src or os.environ.get('SQ2_DEVICE_FAKER_SRC')
    out_zip = Path(out_zip)
    if src_override:
        data = build_from_source(Path(src_override), out_zip=out_zip)
        sha = validate_device_faker_zip(data)
        return data, {
            'source': 'fork-source-build',
            'path': str(out_zip),
            'sha256': sha,
            'fork_url': FORK_URL,
            'fork_commit': FORK_COMMIT,
        }

    if out_zip.is_file() and os.environ.get('SQ2_DEVICE_FAKER_REBUILD') != '1':
        data = out_zip.read_bytes()
        sha = validate_device_faker_zip(data)
        return data, {
            'source': 'cached-out-zip',
            'path': str(out_zip),
            'sha256': sha,
            'fork_url': FORK_URL,
            'fork_commit': FORK_COMMIT,
        }

    if not allow_fetch:
        raise FileNotFoundError(
            f'Forked device_faker zip not staged at {out_zip}; set --device-faker-zip / --device-faker-src or allow fetch'
        )
    src_dir = clone_or_update_fork(DEFAULT_SRC_DIR, FORK_URL, FORK_COMMIT)
    data = build_from_source(src_dir, out_zip=out_zip)
    sha = validate_device_faker_zip(data)
    return data, {
        'source': 'fork-git-build',
        'path': str(out_zip),
        'sha256': sha,
        'fork_url': FORK_URL,
        'fork_commit': FORK_COMMIT,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zip', type=Path, help='Use prebuilt device_faker fork zip')
    parser.add_argument('--source', type=Path, help='Local checkout of device_faker_gfx fork')
    parser.add_argument('--out', type=Path, default=DEFAULT_OUT_ZIP, help='Output zip path')
    parser.add_argument('--no-fetch', action='store_true', help='Disallow cloning from GitHub')
    args = parser.parse_args(argv)
    data, meta = resolve_or_build_device_faker_zip(
        explicit_zip=args.zip,
        explicit_src=args.source,
        out_zip=args.out,
        allow_fetch=not args.no_fetch,
    )
    print(f"Prepared device_faker fork module: {meta['path']} ({len(data)} bytes, sha256={meta['sha256']})")


if __name__ == '__main__':
    main()
