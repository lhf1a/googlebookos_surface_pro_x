#!/usr/bin/env python3
"""Resolve, build, and validate default KernelSU modules for SQ2 builds:
  1. ZygiskNext (latest stable release from Dr-TSNG/ZygiskNext)
  2. sq2-game-opt (CPU/GPU scheduler boost + thread pinning + Vulkan/EGL optimizer)
  3. device_faker_gfx (latest main from lhf1a/device_faker_gfx with optional LKM)
"""
import argparse
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import urllib.error
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ZYGISK_NEXT_REPO = 'Dr-TSNG/ZygiskNext'
ZYGISK_NEXT_RELEASES_API = f'https://api.github.com/repos/{ZYGISK_NEXT_REPO}/releases/latest'
DEFAULT_ZYGISK_ZIP = ROOT / 'out' / 'ksu-modules' / 'zygisk-next.zip'
DEFAULT_ZYGISK_META = ROOT / 'out' / 'ksu-modules' / 'zygisk-next.meta.json'
DEFAULT_GAME_OPT_ZIP = ROOT / 'out' / 'sq2-game-opt' / 'sq2-game-opt.zip'

ZYGISK_NEXT_REQUIRED_ENTRIES = {
    'module.prop',
    'customize.sh',
    'lib/arm64-v8a/libzygisk.so',
    'bin/arm64-v8a/zygiskd',
}
GAME_OPT_REQUIRED_ENTRIES = {
    'module.prop',
    'customize.sh',
    'bin/sq2-game-optd',
    'zygisk/arm64-v8a.so',
}


def _assert_aarch64_elf(blob, label):
    if len(blob) < 64 or blob[:6] != b'\x7fELF\x02\x01':
        raise ValueError(f'{label} is not ELF64 little-endian')
    machine = struct.unpack_from('<H', blob, 18)[0]
    if machine != 183:
        raise ValueError(f'{label} expected EM_AARCH64 (183), got {machine}')


def _parse_module_prop(prop_text):
    props = {}
    for line in prop_text.splitlines():
        line = line.strip()
        if not line or line.startswith('#') or '=' not in line:
            continue
        k, v = line.split('=', 1)
        props[k.strip()] = v.strip()
    return props


def validate_zygisk_next_zip(data):
    """Verify ZygiskNext ZIP contains module.prop (id=zygisksu) and AArch64 libzygisk.so/zygiskd."""
    if not data:
        raise ValueError('Empty ZygiskNext zip')
    with zipfile.ZipFile(io.BytesIO(data), 'r') as zf:
        names = set(zf.namelist())
        missing = ZYGISK_NEXT_REQUIRED_ENTRIES - names
        if missing:
            raise ValueError(f'ZygiskNext zip missing required entries: {sorted(missing)}')
        props = _parse_module_prop(zf.read('module.prop').decode('utf-8', errors='replace'))
        if props.get('id') != 'zygisksu':
            raise ValueError(f"ZygiskNext module.prop expected id=zygisksu, got {props.get('id')!r}")
        _assert_aarch64_elf(zf.read('lib/arm64-v8a/libzygisk.so'), 'ZygiskNext lib/arm64-v8a/libzygisk.so')
        _assert_aarch64_elf(zf.read('bin/arm64-v8a/zygiskd'), 'ZygiskNext bin/arm64-v8a/zygiskd')
    return hashlib.sha256(data).hexdigest(), props


def validate_game_opt_zip(data):
    """Verify sq2-game-opt ZIP contains module.prop (id=sq2-game-opt) and AArch64 daemon/Zygisk lib."""
    if not data:
        raise ValueError('Empty sq2-game-opt zip')
    with zipfile.ZipFile(io.BytesIO(data), 'r') as zf:
        names = set(zf.namelist())
        missing = GAME_OPT_REQUIRED_ENTRIES - names
        if missing:
            raise ValueError(f'sq2-game-opt zip missing required entries: {sorted(missing)}')
        props = _parse_module_prop(zf.read('module.prop').decode('utf-8', errors='replace'))
        if props.get('id') != 'sq2-game-opt':
            raise ValueError(f"sq2-game-opt module.prop expected id=sq2-game-opt, got {props.get('id')!r}")
        _assert_aarch64_elf(zf.read('bin/sq2-game-optd'), 'sq2-game-opt bin/sq2-game-optd')
        _assert_aarch64_elf(zf.read('zygisk/arm64-v8a.so'), 'sq2-game-opt zygisk/arm64-v8a.so')
    return hashlib.sha256(data).hexdigest(), props


def download_url_bytes(url, timeout=180, headers=()):
    curl = shutil.which('curl')
    if curl:
        cmd = [
            curl, '-fsSL', '--retry', '2',
            '--connect-timeout', '20',
            '--max-time', str(timeout),
            '-A', 'chromebookOS-sq2-builder',
        ]
        for k, v in headers:
            cmd.extend(['-H', f'{k}: {v}'])
        cmd.append(url)
        res = subprocess.run(cmd, capture_output=True)
        if res.returncode != 0:
            err = res.stderr.decode('utf-8', errors='replace').strip()
            raise OSError(f'curl failed ({res.returncode}) for {url}: {err}')
        return res.stdout
    req_headers = {'User-Agent': 'chromebookOS-sq2-builder'}
    req_headers.update(dict(headers))
    req = urllib.request.Request(url, headers=req_headers)
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.read()


def fetch_latest_zygisk_next_release(api_url=ZYGISK_NEXT_RELEASES_API, timeout=30):
    """Query GitHub latest stable release of Dr-TSNG/ZygiskNext and return (tag, asset_name, download_url)."""
    raw = download_url_bytes(
        api_url,
        timeout=timeout,
        headers=(('Accept', 'application/vnd.github+json'),),
    )
    release = json.loads(raw.decode('utf-8'))
    if release.get('prerelease') or release.get('draft'):
        raise ValueError(f"Latest ZygiskNext release is not stable: {release.get('tag_name')}")
    tag = release.get('tag_name') or ''
    assets = release.get('assets') or []
    release_zips = [
        a for a in assets
        if a.get('name', '').endswith('.zip') and 'release' in a.get('name', '').lower()
    ]
    zips = release_zips or [a for a in assets if a.get('name', '').endswith('.zip')]
    if not zips:
        raise ValueError(f'No ZIP asset found in ZygiskNext release {tag}')
    asset = zips[0]
    return tag, asset['name'], asset['browser_download_url']


def resolve_or_fetch_zygisk_next_zip(
    private=None,
    explicit_zip=None,
    out_zip=DEFAULT_ZYGISK_ZIP,
    allow_fetch=True,
):
    """Resolve ZygiskNext stable KSU zip: explicit > private > latest GitHub stable release > cache."""
    zip_path = explicit_zip or os.environ.get('SQ2_ZYGISK_NEXT_ZIP')
    if zip_path:
        p = Path(zip_path)
        data = p.read_bytes()
        sha, props = validate_zygisk_next_zip(data)
        return data, {
            'source': 'explicit-zip',
            'path': str(p),
            'sha256': sha,
            'version': props.get('version'),
            'repo': ZYGISK_NEXT_REPO,
        }

    if private is not None:
        for cand_name in ('zygisk-next.zip', 'zygisk_next.zip', 'ZygiskNext.zip'):
            priv_zip = Path(private) / 'ksu-modules' / cand_name
            if priv_zip.is_file():
                data = priv_zip.read_bytes()
                sha, props = validate_zygisk_next_zip(data)
                return data, {
                    'source': 'private-ksu-modules',
                    'path': str(priv_zip),
                    'sha256': sha,
                    'version': props.get('version'),
                    'repo': ZYGISK_NEXT_REPO,
                }

    out_zip = Path(out_zip)
    meta_path = out_zip.with_suffix('.meta.json')
    use_cache_first = os.environ.get('SQ2_KSU_MODULES_USE_CACHE') == '1'

    if allow_fetch and not use_cache_first:
        try:
            tag, asset_name, dl_url = fetch_latest_zygisk_next_release()
            cached_meta = {}
            if meta_path.is_file():
                try:
                    cached_meta = json.loads(meta_path.read_text(encoding='utf-8'))
                except (ValueError, OSError):
                    cached_meta = {}
            if (
                out_zip.is_file()
                and cached_meta.get('tag') == tag
                and cached_meta.get('asset') == asset_name
            ):
                data = out_zip.read_bytes()
                sha, props = validate_zygisk_next_zip(data)
                return data, {
                    'source': 'github-stable-cached',
                    'path': str(out_zip),
                    'sha256': sha,
                    'tag': tag,
                    'asset': asset_name,
                    'url': dl_url,
                    'version': props.get('version'),
                    'repo': ZYGISK_NEXT_REPO,
                }
            data = download_url_bytes(dl_url)
            sha, props = validate_zygisk_next_zip(data)
            out_zip.parent.mkdir(parents=True, exist_ok=True)
            out_zip.write_bytes(data)
            meta_path.write_text(
                json.dumps({'tag': tag, 'asset': asset_name, 'url': dl_url, 'sha256': sha}, indent=2) + '\n',
                encoding='utf-8',
            )
            return data, {
                'source': 'github-stable-release',
                'path': str(out_zip),
                'sha256': sha,
                'tag': tag,
                'asset': asset_name,
                'url': dl_url,
                'version': props.get('version'),
                'repo': ZYGISK_NEXT_REPO,
            }
        except (urllib.error.URLError, OSError, ValueError) as exc:
            print(
                f'WARNING: failed to fetch latest stable ZygiskNext from {ZYGISK_NEXT_REPO}: {exc}; falling back to local cache',
                file=sys.stderr,
            )

    if out_zip.is_file():
        data = out_zip.read_bytes()
        sha, props = validate_zygisk_next_zip(data)
        return data, {
            'source': 'cached-out-zip',
            'path': str(out_zip),
            'sha256': sha,
            'version': props.get('version'),
            'repo': ZYGISK_NEXT_REPO,
        }

    raise FileNotFoundError(
        f'ZygiskNext stable zip not staged at {out_zip}; set --zygisk-next-zip or allow GitHub fetch'
    )


def _load_game_opt_builder():
    script_path = ROOT / 'scripts' / 'build-sq2-game-opt.py'
    spec = importlib.util.spec_from_file_location('sq2_game_opt_builder', script_path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def resolve_or_build_game_opt_zip(
    private=None,
    explicit_zip=None,
    out_zip=DEFAULT_GAME_OPT_ZIP,
):
    """Resolve or compile sq2-game-opt KSU zip (CPU/GPU scheduler + Vulkan/EGL optimizer)."""
    zip_path = explicit_zip or os.environ.get('SQ2_GAME_OPT_ZIP')
    if zip_path:
        p = Path(zip_path)
        data = p.read_bytes()
        sha, props = validate_game_opt_zip(data)
        return data, {
            'source': 'explicit-zip',
            'path': str(p),
            'sha256': sha,
            'version': props.get('version'),
        }

    if private is not None:
        priv_zip = Path(private) / 'ksu-modules' / 'sq2-game-opt.zip'
        if priv_zip.is_file():
            data = priv_zip.read_bytes()
            sha, props = validate_game_opt_zip(data)
            return data, {
                'source': 'private-ksu-modules',
                'path': str(priv_zip),
                'sha256': sha,
                'version': props.get('version'),
            }

    out_zip = Path(out_zip)
    builder = _load_game_opt_builder()
    built = builder.build_package(out_zip=out_zip)
    data = Path(built).read_bytes()
    sha, props = validate_game_opt_zip(data)
    return data, {
        'source': 'source-build',
        'path': str(built),
        'sha256': sha,
        'version': props.get('version'),
    }


def resolve_default_ksu_modules(
    private=None,
    skip_zygisk_next=False,
    zygisk_next_zip=None,
    skip_game_opt=False,
    game_opt_zip=None,
    skip_device_faker=False,
    device_faker_zip=None,
    device_faker_src=None,
    kernel_dir=None,
    cross_compile=None,
    allow_fetch=True,
):
    """Prepare ordered default KSU modules for initramfs/etc/sq2-ksu-modules/.

    Numeric prefixes enforce deterministic installation order in sq2-ksu-modules-boot:
      10-zygisk-next.zip   (must install first so libzygisk.so exists for customize.sh checks)
      20-sq2-game-opt.zip  (SQ2 CPU/GPU scheduler & game optimizer)
      30-device_faker.zip  (device_faker_gfx latest main + LKM)
    """
    from build_device_faker_fork import resolve_or_build_device_faker_zip

    ksu_modules = {}
    meta = {
        'zygisk_next': None,
        'sq2_game_opt': None,
        'device_faker_fork': None,
    }

    if not skip_zygisk_next:
        zn_bytes, zn_meta = resolve_or_fetch_zygisk_next_zip(
            private=private,
            explicit_zip=zygisk_next_zip,
            allow_fetch=allow_fetch,
        )
        ksu_modules['10-zygisk-next.zip'] = zn_bytes
        meta['zygisk_next'] = zn_meta

    if not skip_game_opt:
        go_bytes, go_meta = resolve_or_build_game_opt_zip(
            private=private,
            explicit_zip=game_opt_zip,
        )
        ksu_modules['20-sq2-game-opt.zip'] = go_bytes
        meta['sq2_game_opt'] = go_meta

    if not skip_device_faker:
        df_bytes, df_meta = resolve_or_build_device_faker_zip(
            private=private,
            explicit_zip=device_faker_zip,
            explicit_src=device_faker_src,
            allow_fetch=allow_fetch,
            kernel_dir=kernel_dir,
            cross_compile=cross_compile,
        )
        ksu_modules['30-device_faker.zip'] = df_bytes
        meta['device_faker_fork'] = df_meta

    return ksu_modules, meta


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kernel-dir', type=Path, help='Kernel build directory (DF_KERNEL_DIR) for df_gpu_vfs.ko')
    parser.add_argument('--cross-compile', help='CROSS_COMPILE prefix for df_gpu_vfs.ko')
    parser.add_argument('--no-fetch', action='store_true', help='Disallow GitHub network fetch')
    parser.add_argument('--skip-zygisk-next', action='store_true')
    parser.add_argument('--skip-game-opt', action='store_true')
    parser.add_argument('--skip-device-faker', action='store_true')
    args = parser.parse_args(argv)
    modules, meta = resolve_default_ksu_modules(
        skip_zygisk_next=args.skip_zygisk_next,
        skip_game_opt=args.skip_game_opt,
        skip_device_faker=args.skip_device_faker,
        kernel_dir=args.kernel_dir,
        cross_compile=args.cross_compile,
        allow_fetch=not args.no_fetch,
    )
    print(json.dumps({'staged': sorted(modules.keys()), 'meta': meta}, indent=2))


if __name__ == '__main__':
    main()
