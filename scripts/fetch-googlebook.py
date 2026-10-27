#!/usr/bin/env python3
"""Fetch a pinned official recovery, verify ZIP and RAW, never write a device."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def checksum(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def verify(path, expected):
    if not path.is_file() or path.is_symlink():
        raise ValueError(f'Not a regular file: {path}')
    actual = checksum(path)
    if actual != expected:
        raise ValueError(f'Checksum mismatch for {path.name}: {actual}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--extract', action='store_true')
    args = parser.parse_args()
    spec = json.loads((ROOT / 'configs/sources.lock.json').read_text())['googlebook']
    folder = ROOT / 'downloads'
    folder.mkdir(exist_ok=True)
    archive = folder / 'mica-recovery.zip'
    if not archive.exists():
        partial = folder / 'mica-recovery.zip.part'
        if partial.is_symlink():
            raise ValueError('Refusing symlink download target')
        if shutil.disk_usage(folder).free < spec['zip_bytes'] - (partial.stat().st_size if partial.exists() else 0):
            raise ValueError('Not enough free space for download')
        have = partial.stat().st_size if partial.exists() else 0
        if have > spec['zip_bytes']:
            raise ValueError('Partial file exceeds expected archive size')
        if have < spec['zip_bytes']:
            subprocess.run(['curl', '--fail', '--location', '--retry', '3', '--connect-timeout', '30', '--continue-at', '-', '--output', str(partial), spec['url']], check=True)
        verify(partial, spec['zip_sha256'])
        partial.rename(archive)
    else:
        verify(archive, spec['zip_sha256'])
    print('Official recovery ZIP checksum verified.')
    if args.extract:
        raw = folder / 'mica-recovery.raw'
        if raw.exists():
            verify(raw, spec['raw_sha256'])
            return
        temporary = folder / 'mica-recovery.raw.part'
        with zipfile.ZipFile(archive) as z:
            member = z.getinfo('android-desktop_signed_recovery_image.bin')
            if shutil.disk_usage(folder).free < member.file_size:
                raise ValueError('Not enough free space for RAW image')
            with z.open(member) as src, temporary.open('xb') as dst:
                shutil.copyfileobj(src, dst, 8 << 20)
        verify(temporary, spec['raw_sha256'])
        temporary.rename(raw)
        print('Official recovery RAW checksum verified.')


if __name__ == '__main__':
    main()
