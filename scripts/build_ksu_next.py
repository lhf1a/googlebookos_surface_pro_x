#!/usr/bin/env python3
"""Fetch/stage latest stable KernelSU-Next (kernel/ source + ksud + Manager APK) for source builds."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import urllib.error
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
KSU_NEXT_REPO = 'KernelSU-Next/KernelSU-Next'
KSU_NEXT_GIT_URL = f'https://github.com/{KSU_NEXT_REPO}.git'
KSU_NEXT_RELEASES_API = f'https://api.github.com/repos/{KSU_NEXT_REPO}/releases/latest'

DEFAULT_SRC_DIR = ROOT / 'out' / 'src' / 'KernelSU-Next'
DEFAULT_CACHE_DIR = ROOT / 'out' / 'ksu-next'


def _assert_aarch64_elf(blob, label):
    if len(blob) < 64 or blob[:6] != b'\x7fELF\x02\x01':
        raise ValueError(f'{label} is not ELF64 little-endian')
    machine = struct.unpack_from('<H', blob, 18)[0]
    if machine != 183:
        raise ValueError(f'{label} expected EM_AARCH64 (183), got {machine}')


def validate_ksud_bytes(data):
    if not data:
        raise ValueError('Empty ksud binary')
    _assert_aarch64_elf(data, 'ksud')
    return hashlib.sha256(data).hexdigest()


def validate_manager_apk_bytes(data):
    if not data:
        raise ValueError('Empty KernelSU-Next Manager APK')
    with zipfile.ZipFile(io.BytesIO(data), 'r') as zf:
        names = set(zf.namelist())
        if 'AndroidManifest.xml' not in names:
            raise ValueError('KernelSU-Next Manager APK missing AndroidManifest.xml')
    return hashlib.sha256(data).hexdigest()


def parse_manager_apk_asset(assets):
    """Select the official (non-spoofed) release APK asset and extract its numeric version code."""
    for asset in assets:
        name = asset.get('name', '')
        if not name.endswith('.apk') or 'spoofed' in name.lower():
            continue
        m = re.search(r'_(\d+)-release\.apk$', name)
        if m:
            return int(m.group(1)), name, asset['browser_download_url']
    raise ValueError('No official KernelSU-Next release APK found in release assets')


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


def fetch_latest_ksu_next_release(api_url=KSU_NEXT_RELEASES_API, timeout=30):
    """Query GitHub latest stable release of KernelSU-Next."""
    raw = download_url_bytes(
        api_url,
        timeout=timeout,
        headers=(('Accept', 'application/vnd.github+json'),),
    )
    release = json.loads(raw.decode('utf-8'))
    if release.get('prerelease') or release.get('draft'):
        raise ValueError(f"Latest KernelSU-Next release is not stable: {release.get('tag_name')}")
    tag = release.get('tag_name') or ''
    if not tag:
        raise ValueError('KernelSU-Next release missing tag_name')
    assets = release.get('assets') or []
    version_code, apk_name, apk_url = parse_manager_apk_asset(assets)
    ksud_assets = [a for a in assets if a.get('name') == 'ksud-aarch64-linux-android']
    if not ksud_assets:
        raise ValueError(f'No ksud-aarch64-linux-android asset in KernelSU-Next release {tag}')
    ksud_asset = ksud_assets[0]
    return {
        'tag': tag,
        'version_code': version_code,
        'apk_name': apk_name,
        'apk_url': apk_url,
        'ksud_name': ksud_asset['name'],
        'ksud_url': ksud_asset['browser_download_url'],
    }


def clone_or_update_ksu_source(src_dir=DEFAULT_SRC_DIR, git_url=KSU_NEXT_GIT_URL, tag='v3.4.2'):
    """Shallow-clone or fetch the target KernelSU-Next release tag into src_dir."""
    src_dir = Path(src_dir)
    if (src_dir / '.git').is_dir():
        subprocess.run(
            ['git', '-C', str(src_dir), 'fetch', '--depth', '1', git_url, f'refs/tags/{tag}'],
            check=True,
        )
        subprocess.run(['git', '-C', str(src_dir), 'checkout', '--detach', 'FETCH_HEAD'], check=True)
    else:
        src_dir.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            ['git', 'clone', '--depth', '1', '--branch', tag, git_url, str(src_dir)],
            check=True,
        )
    if not (src_dir / 'kernel' / 'Kbuild').is_file():
        raise FileNotFoundError(f'Invalid KernelSU-Next checkout (missing kernel/Kbuild): {src_dir}')
    return src_dir


def patch_ksu_kbuild_version(kbuild_path, tag, version_code):
    """Inject exact release version_code and tag into Kbuild fallbacks (for non-git kernel copy)."""
    text = Path(kbuild_path).read_text(encoding='utf-8')
    new_text, n_ver = re.subn(
        r'^KSU_VERSION_FALLBACK\s*:=\s*.*$',
        f'KSU_VERSION_FALLBACK := {int(version_code)}',
        text,
        flags=re.M,
    )
    new_text, n_tag = re.subn(
        r'^KSU_VERSION_TAG_FALLBACK\s*:=\s*.*$',
        f'KSU_VERSION_TAG_FALLBACK := {tag}',
        new_text,
        flags=re.M,
    )
    if n_ver != 1 or n_tag != 1:
        raise RuntimeError(
            f'Unexpected Kbuild fallback layout in {kbuild_path} (n_ver={n_ver}, n_tag={n_tag})'
        )
    Path(kbuild_path).write_text(new_text, encoding='utf-8')


def apply_to_kernel_source(kernel_dir, ksu_src_dir, tag, version_code):
    """Replace kernel_dir/drivers/kernelsu with KernelSU-Next kernel/ and stamp exact version."""
    kernel_dir = Path(kernel_dir)
    ksu_kernel_src = Path(ksu_src_dir) / 'kernel'
    if not (ksu_kernel_src / 'Kbuild').is_file():
        raise FileNotFoundError(f'Missing KernelSU-Next kernel/Kbuild under {ksu_src_dir}')

    drivers_dir = kernel_dir / 'drivers'
    dst = drivers_dir / 'kernelsu'
    if dst.exists() or dst.is_symlink():
        if dst.is_symlink():
            dst.unlink()
        else:
            shutil.rmtree(dst)
    shutil.copytree(
        ksu_kernel_src,
        dst,
        ignore=shutil.ignore_patterns('*.o', '*.ko', '*.mod*', '.*.cmd', 'Module.symvers', 'modules.order', '.git'),
    )
    patch_ksu_kbuild_version(dst / 'Kbuild', tag=tag, version_code=version_code)

    makefile = drivers_dir / 'Makefile'
    if 'kernelsu/' not in makefile.read_text(encoding='utf-8'):
        with makefile.open('a', encoding='utf-8') as f:
            f.write('\nobj-$(CONFIG_KSU) += kernelsu/\n')
    kconfig = drivers_dir / 'Kconfig'
    if 'drivers/kernelsu/Kconfig' not in kconfig.read_text(encoding='utf-8'):
        kc = kconfig.read_text(encoding='utf-8')
        idx = kc.rfind('endmenu')
        if idx < 0:
            raise RuntimeError(f'drivers/Kconfig missing endmenu: {kconfig}')
        kc = kc[:idx] + 'source "drivers/kernelsu/Kconfig"\n' + kc[idx:]
        kconfig.write_text(kc, encoding='utf-8')
    return dst


def resolve_latest_ksu_next(
    explicit_src=None,
    cache_dir=DEFAULT_CACHE_DIR,
    src_dir=DEFAULT_SRC_DIR,
    allow_fetch=True,
):
    """Resolve KernelSU-Next latest stable release (kernel source dir + ksud bytes + manager APK bytes)."""
    cache_dir = Path(cache_dir)
    cache_dir.mkdir(parents=True, exist_ok=True)
    meta_file = cache_dir / 'ksu-next.meta.json'
    ksud_path = cache_dir / 'ksud'
    apk_path = cache_dir / 'manager-verified.apk'

    cached_meta = {}
    if meta_file.is_file():
        try:
            cached_meta = json.loads(meta_file.read_text(encoding='utf-8'))
        except (ValueError, OSError):
            cached_meta = {}

    release = None
    source_kind = 'github-stable-release'
    if allow_fetch and os.environ.get('SQ2_KSU_NEXT_USE_CACHE') != '1':
        try:
            release = fetch_latest_ksu_next_release()
        except (urllib.error.URLError, OSError, ValueError) as exc:
            print(
                f'WARNING: failed to query {KSU_NEXT_REPO} latest stable release: {exc}; trying local cache',
                file=sys.stderr,
            )

    if release is not None:
        tag = release['tag']
        version_code = release['version_code']
        if (
            cached_meta.get('tag') == tag
            and cached_meta.get('version_code') == version_code
            and ksud_path.is_file()
            and apk_path.is_file()
        ):
            ksud_bytes = ksud_path.read_bytes()
            apk_bytes = apk_path.read_bytes()
            ksud_sha = validate_ksud_bytes(ksud_bytes)
            apk_sha = validate_manager_apk_bytes(apk_bytes)
            source_kind = 'github-stable-cached'
        else:
            ksud_bytes = download_url_bytes(release['ksud_url'])
            ksud_sha = validate_ksud_bytes(ksud_bytes)
            apk_bytes = download_url_bytes(release['apk_url'])
            apk_sha = validate_manager_apk_bytes(apk_bytes)
            ksud_path.write_bytes(ksud_bytes)
            apk_path.write_bytes(apk_bytes)
            meta_file.write_text(
                json.dumps(
                    {
                        'repo': KSU_NEXT_REPO,
                        'tag': tag,
                        'version_code': version_code,
                        'ksud_name': release['ksud_name'],
                        'ksud_url': release['ksud_url'],
                        'ksud_sha256': ksud_sha,
                        'apk_name': release['apk_name'],
                        'apk_url': release['apk_url'],
                        'apk_sha256': apk_sha,
                    },
                    indent=2,
                )
                + '\n',
                encoding='utf-8',
            )

        if explicit_src:
            resolved_src = Path(explicit_src)
        else:
            resolved_src = clone_or_update_ksu_source(src_dir=src_dir, tag=tag)
    else:
        if not (cached_meta.get('tag') and cached_meta.get('version_code') and ksud_path.is_file() and apk_path.is_file()):
            raise FileNotFoundError(
                f'KernelSU-Next stable release not cached under {cache_dir}; allow network fetch or pass --skip-ksu-upgrade'
            )
        tag = cached_meta['tag']
        version_code = int(cached_meta['version_code'])
        ksud_bytes = ksud_path.read_bytes()
        apk_bytes = apk_path.read_bytes()
        ksud_sha = validate_ksud_bytes(ksud_bytes)
        apk_sha = validate_manager_apk_bytes(apk_bytes)
        resolved_src = Path(explicit_src) if explicit_src else Path(src_dir)
        if not (resolved_src / 'kernel' / 'Kbuild').is_file():
            raise FileNotFoundError(f'Cached KernelSU-Next kernel source missing at {resolved_src}')
        source_kind = 'local-cache'

    return {
        'repo': KSU_NEXT_REPO,
        'source': source_kind,
        'tag': tag,
        'version_code': version_code,
        'src_dir': resolved_src,
        'ksud_bytes': ksud_bytes,
        'ksud_sha256': ksud_sha,
        'apk_bytes': apk_bytes,
        'apk_sha256': apk_sha,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, help='Use local KernelSU-Next checkout')
    parser.add_argument('--no-fetch', action='store_true', help='Use local cache only')
    args = parser.parse_args(argv)
    info = resolve_latest_ksu_next(explicit_src=args.source, allow_fetch=not args.no_fetch)
    printable = {k: str(v) if isinstance(v, Path) else v for k, v in info.items() if not k.endswith('_bytes')}
    print(json.dumps(printable, indent=2))


if __name__ == '__main__':
    main()
