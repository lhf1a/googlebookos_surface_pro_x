#!/usr/bin/env python3
"""Fetch the pinned official donor archive; no device or original image writes."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import time
import urllib.request
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parents[1]
spec = json.loads((ROOT / 'configs/sources.lock.json').read_text())['cuttlefish_security_donor']
folder = ROOT / 'downloads'
folder.mkdir(exist_ok=True)
target = folder / ('cuttlefish-' + spec['build'] + '.zip')
partial = target.with_suffix('.zip.part')
assert not target.is_symlink() and not partial.is_symlink()

if not target.exists():
    html = urllib.request.urlopen(spec['url'], timeout=30).read(2 << 20).decode()
    metadata = json.loads(re.search(r'var JSVariables = (\{.*?\});', html).group(1))
    url = metadata['artifactUrl']
    parsed = urlparse(url)
    assert parsed.scheme == 'https'
    assert parsed.hostname == 'storage.googleapis.com' or parsed.hostname.endswith('.googleusercontent.com')
    offset = partial.stat().st_size if partial.exists() else 0
    request = urllib.request.Request(url, headers={'Range': f'bytes={offset}-'} if offset else {})
    with urllib.request.urlopen(request, timeout=60) as response:
        if offset:
            assert response.status == 206
            assert response.headers['Content-Range'].startswith(f'bytes {offset}-')
        remaining = int(response.headers.get('Content-Length', 0))
        assert shutil.disk_usage(folder).free > remaining + (1 << 30)
        print(f'Downloading pinned Cuttlefish archive: existing={offset}, remaining={remaining}', flush=True)
        start = last = time.monotonic()
        with partial.open('ab' if offset else 'xb') as output:
            while chunk := response.read(4 << 20):
                output.write(chunk)
                offset += len(chunk)
                if time.monotonic() - last >= 20:
                    print(f'Downloaded {offset / (1 << 20):.0f} MiB', flush=True)
                    last = time.monotonic()
    with partial.open('rb') as source:
        digest = hashlib.file_digest(source, 'sha256').hexdigest()
    assert digest == spec['zip_sha256'], f'Archive checksum mismatch: {digest}'
    partial.rename(target)
else:
    with target.open('rb') as source:
        assert hashlib.file_digest(source, 'sha256').hexdigest() == spec['zip_sha256']
print(f'Archive verified: {target.name}', flush=True)
