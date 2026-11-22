#!/usr/bin/env python3
"""Fetch/build and verify the forked device_faker_gfx KernelSU module (tracks latest main by default)."""
import argparse
import hashlib
import io
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
FORK_URL = 'https://github.com/lhf1a/device_faker_gfx.git'
FORK_BRANCH = 'main'
SUBMODULE_SRC_DIR = ROOT / 'upstream' / 'device_faker_gfx'
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


def clone_or_update_fork(src_dir=DEFAULT_SRC_DIR, fork_url=FORK_URL, branch=FORK_BRANCH, commit=None):
    """Clone or update device_faker_gfx into src_dir, tracking latest branch HEAD when commit is None."""
    src_dir = Path(src_dir)
    if (src_dir / '.git').is_dir():
        refspec = commit if commit else f'refs/heads/{branch}'
        subprocess.run(['git', '-C', str(src_dir), 'fetch', '--depth', '1', fork_url, refspec], check=True)
        if commit:
            subprocess.run(['git', '-C', str(src_dir), 'checkout', '--detach', commit], check=True)
        else:
            subprocess.run(['git', '-C', str(src_dir), 'checkout', '-B', branch, 'FETCH_HEAD'], check=True)
    else:
        src_dir.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            ['git', 'clone', '--depth', '1', '--branch', branch, fork_url, str(src_dir)],
            check=True,
        )
        if commit:
            subprocess.run(['git', '-C', str(src_dir), 'fetch', '--depth', '1', fork_url, commit], check=True)
            subprocess.run(['git', '-C', str(src_dir), 'checkout', '--detach', commit], check=True)
    return src_dir


def _discover_ndk_home(env):
    """Locate a readable Android NDK root from environment or standard workspace/cache locations."""
    for var in ('ANDROID_NDK_HOME', 'ANDROID_NDK_ROOT', 'NDK_HOME'):
        val = env.get(var)
        if val and (Path(val) / 'toolchains').is_dir():
            return val
    cc = env.get('SQ2_ANDROID_CC')
    if cc:
        for parent in Path(cc).resolve().parents:
            if (parent / 'toolchains').is_dir() and (parent / 'build').is_dir():
                return str(parent)
    static_candidates = [
        Path('/mnt/spx-workspace/toolchains/sq2-graphics/android-ndk-r28c'),
        Path('/root/.cache/sq2-graphics/android-ndk-r28c'),
    ]
    for cand in static_candidates:
        try:
            if (cand / 'toolchains').is_dir():
                return str(cand)
        except PermissionError:
            continue
    cache_dir = Path.home() / '.cache'
    if cache_dir.is_dir():
        for cand in sorted(cache_dir.glob('ndk-*/android-ndk-*'), reverse=True):
            if (cand / 'toolchains').is_dir():
                return str(cand)
    return None


def _augment_build_path(env):
    """Ensure ~/.cargo/bin and nvm node bin are present in PATH for non-interactive shells."""
    extra_dirs = []
    cargo_bin = Path.home() / '.cargo' / 'bin'
    if cargo_bin.is_dir():
        extra_dirs.append(str(cargo_bin))
    if not shutil.which('npm', path=env.get('PATH')):
        nvm_versions = Path.home() / '.nvm' / 'versions' / 'node'
        if nvm_versions.is_dir():
            for node_dir in sorted(nvm_versions.iterdir(), reverse=True):
                bin_dir = node_dir / 'bin'
                if (bin_dir / 'npm').is_file():
                    extra_dirs.append(str(bin_dir))
                    break
    if extra_dirs:
        env['PATH'] = ':'.join(extra_dirs) + ':' + env.get('PATH', '')


def build_from_source(source_dir, out_zip=DEFAULT_OUT_ZIP, kernel_dir=None, cross_compile=None):
    """Build device_faker_gfx-(release).zip via xtask (optionally bundling df_gpu_vfs.ko) and write out_zip."""
    source_dir = Path(source_dir)
    out_zip = Path(out_zip)
    if not (source_dir / 'Cargo.toml').is_file():
        raise FileNotFoundError(f'Not a device_faker source checkout: {source_dir}')
    env = dict(os.environ)
    _augment_build_path(env)
    ndk_home = _discover_ndk_home(env)
    if ndk_home:
        env.setdefault('ANDROID_NDK_HOME', ndk_home)
    kdir = kernel_dir or env.get('DF_KERNEL_DIR') or env.get('KDIR')
    if kdir and Path(kdir).is_dir():
        env['DF_KERNEL_DIR'] = str(Path(kdir).resolve())
        lkm_dir = source_dir / 'lkm'
        if (lkm_dir / 'Makefile').is_file():
            for artifact in lkm_dir.iterdir():
                if not artifact.is_file():
                    continue
                name = artifact.name
                if (
                    name in ('Module.symvers', 'modules.order')
                    or name.endswith(('.ko', '.o', '.mod', '.mod.c', '.cmd'))
                ):
                    artifact.unlink()
    if cross_compile:
        env['CROSS_COMPILE'] = str(cross_compile)
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


def _read_git_head(source_dir):
    """Return HEAD commit SHA for a git checkout or submodule, or None if unavailable."""
    res = subprocess.run(
        ['git', '-C', str(source_dir), 'rev-parse', 'HEAD'],
        capture_output=True,
        text=True,
    )
    if res.returncode == 0 and res.stdout.strip():
        return res.stdout.strip()
    return None


def _zip_contains_lkm(data):
    with zipfile.ZipFile(io.BytesIO(data), 'r') as zf:
        return 'lkm/df_gpu_vfs.ko' in set(zf.namelist())


def resolve_or_build_device_faker_zip(
    private=None,
    explicit_zip=None,
    explicit_src=None,
    out_zip=DEFAULT_OUT_ZIP,
    allow_fetch=True,
    submodule_dir=SUBMODULE_SRC_DIR,
    kernel_dir=None,
    cross_compile=None,
):
    """Resolve device_faker_gfx KSU zip: explicit override > private > latest main fetch/build > cache/submodule."""
    zip_path = explicit_zip or os.environ.get('SQ2_DEVICE_FAKER_ZIP')
    if zip_path:
        p = Path(zip_path)
        data = p.read_bytes()
        sha = validate_device_faker_zip(data)
        return data, {
            'source': 'explicit-zip',
            'path': str(p),
            'sha256': sha,
            'fork_url': FORK_URL,
            'fork_branch': FORK_BRANCH,
            'lkm_bundled': _zip_contains_lkm(data),
        }

    if private is not None:
        priv_zip = Path(private) / 'ksu-modules' / 'device_faker.zip'
        if priv_zip.is_file():
            data = priv_zip.read_bytes()
            sha = validate_device_faker_zip(data)
            return data, {
                'source': 'private-ksu-modules',
                'path': str(priv_zip),
                'sha256': sha,
                'fork_url': FORK_URL,
                'fork_branch': FORK_BRANCH,
                'lkm_bundled': _zip_contains_lkm(data),
            }

    src_override = explicit_src or os.environ.get('SQ2_DEVICE_FAKER_SRC')
    out_zip = Path(out_zip)
    if src_override:
        src_path = Path(src_override)
        data = build_from_source(
            src_path, out_zip=out_zip, kernel_dir=kernel_dir, cross_compile=cross_compile
        )
        sha = validate_device_faker_zip(data)
        return data, {
            'source': 'fork-source-build',
            'path': str(out_zip),
            'sha256': sha,
            'fork_url': FORK_URL,
            'fork_branch': FORK_BRANCH,
            'fork_commit': _read_git_head(src_path),
            'lkm_bundled': _zip_contains_lkm(data),
        }

    use_cache_first = os.environ.get('SQ2_DEVICE_FAKER_USE_CACHE') == '1'
    if allow_fetch and not use_cache_first:
        try:
            src_dir = clone_or_update_fork(DEFAULT_SRC_DIR, FORK_URL, branch=FORK_BRANCH)
            data = build_from_source(
                src_dir, out_zip=out_zip, kernel_dir=kernel_dir, cross_compile=cross_compile
            )
            sha = validate_device_faker_zip(data)
            return data, {
                'source': 'fork-git-main-build',
                'path': str(out_zip),
                'sha256': sha,
                'fork_url': FORK_URL,
                'fork_branch': FORK_BRANCH,
                'fork_commit': _read_git_head(src_dir),
                'lkm_bundled': _zip_contains_lkm(data),
            }
        except (subprocess.CalledProcessError, OSError) as exc:
            print(
                f'WARNING: failed to fetch/build latest {FORK_URL}@{FORK_BRANCH}: {exc}; falling back to local cache/submodule',
                file=sys.stderr,
            )

    if out_zip.is_file() and os.environ.get('SQ2_DEVICE_FAKER_REBUILD') != '1':
        data = out_zip.read_bytes()
        sha = validate_device_faker_zip(data)
        return data, {
            'source': 'cached-out-zip',
            'path': str(out_zip),
            'sha256': sha,
            'fork_url': FORK_URL,
            'fork_branch': FORK_BRANCH,
            'fork_commit': _read_git_head(DEFAULT_SRC_DIR),
            'lkm_bundled': _zip_contains_lkm(data),
        }

    submod = Path(submodule_dir) if submodule_dir is not None else None
    if submod is not None and (submod / 'Cargo.toml').is_file():
        build_kwargs = {'out_zip': out_zip}
        if kernel_dir is not None:
            build_kwargs['kernel_dir'] = kernel_dir
        if cross_compile is not None:
            build_kwargs['cross_compile'] = cross_compile
        data = build_from_source(submod, **build_kwargs)
        sha = validate_device_faker_zip(data)
        return data, {
            'source': 'fork-submodule-build',
            'path': str(out_zip),
            'sha256': sha,
            'fork_url': FORK_URL,
            'fork_branch': FORK_BRANCH,
            'fork_commit': _read_git_head(submod),
            'lkm_bundled': _zip_contains_lkm(data),
        }

    raise FileNotFoundError(
        f'Forked device_faker zip not staged at {out_zip}; set --device-faker-zip / --device-faker-src or allow fetch'
    )


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zip', type=Path, help='Use prebuilt device_faker_gfx zip')
    parser.add_argument('--source', type=Path, help='Local checkout of device_faker_gfx')
    parser.add_argument('--kernel-dir', type=Path, help='Kernel build directory (DF_KERNEL_DIR) to build df_gpu_vfs.ko')
    parser.add_argument('--cross-compile', help='CROSS_COMPILE prefix for df_gpu_vfs.ko')
    parser.add_argument('--out', type=Path, default=DEFAULT_OUT_ZIP, help='Output zip path')
    parser.add_argument('--no-fetch', action='store_true', help='Disallow fetching latest main from GitHub')
    args = parser.parse_args(argv)
    data, meta = resolve_or_build_device_faker_zip(
        explicit_zip=args.zip,
        explicit_src=args.source,
        out_zip=args.out,
        allow_fetch=not args.no_fetch,
        kernel_dir=args.kernel_dir,
        cross_compile=args.cross_compile,
    )
    print(
        f"Prepared device_faker_gfx module: {meta['path']} "
        f"({len(data)} bytes, sha256={meta['sha256']}, source={meta['source']}, "
        f"commit={meta.get('fork_commit')}, lkm_bundled={meta.get('lkm_bundled')})"
    )


if __name__ == '__main__':
    main()
