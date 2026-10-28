#!/usr/bin/env python3
"""Fetch the two fixed SDK archives already recorded for native test APKs."""
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import zipfile

root = Path(__file__).resolve().parents[1]
work = Path(os.environ.get('SQ2_GRAPHICS_WORK', '/root/.cache/sq2-graphics'))
lock = json.loads((root / 'configs/graphics-sources.lock.json').read_text())['vulkan_test_sdk']
dest = work / 'sdk'
dest.mkdir(parents=True, exist_ok=True)
for name in ['platform', 'build_tools']:
    archive = work / (name + '-test-sdk.zip')
    if not archive.exists():
        print(f'Downloading {name}', flush=True)
        curl=shutil.which('curl.exe') or 'curl'
        partial=archive.with_suffix('.part')
        with partial.open('wb') as output:
            subprocess.run([curl,'-fsSL','--retry','2','--connect-timeout','15','--max-time','180',lock[name+'_url']],stdout=output,check=True)
        partial.replace(archive)
    if hashlib.sha1(archive.read_bytes()).hexdigest()!=lock[name+'_sha1']:
        raise SystemExit(f'{name}: hash mismatch; preserve archive for inspection')
    with zipfile.ZipFile(archive) as z:
        for item in z.infolist():
            path=PurePosixPath(item.filename)
            if path.is_absolute() or '..' in path.parts:
                raise SystemExit('Unsafe archive member')
        z.extractall(dest)
        for item in z.infolist():
            mode=(item.external_attr>>16)&0o777
            if mode:(dest/item.filename).chmod(mode)
    print(f'{name}: verified and extracted',flush=True)
