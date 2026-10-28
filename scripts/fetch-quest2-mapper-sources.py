#!/usr/bin/env python3
"""Fetch pinned public interface/build inputs for the isolated QTI mapper study.

Run in WSL. Downloads and extracted third-party sources remain in the cache;
the cache manifest records hashes. Does not fetch or distribute GPU binaries.
"""
import base64
from concurrent.futures import ThreadPoolExecutor
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
from urllib.request import urlopen

ROOT = Path(os.environ.get('SQ2_QUEST2_WORK', '/root/.cache/sq2-quest2-hidl'))
ROOT.mkdir(parents=True, exist_ok=True)
manifest = json.loads((Path(__file__).resolve().parents[1] / 'configs/quest2-gpu-research.json').read_text())
expected = {item['url']: item['decoded_sha256'] for item in manifest['build_sources']['public_interface_inputs']}
TAG = 'android-12.0.0_r1'
TOOL = '94dfb8dcf347e4fa1d8fda6b0a9b745a4c1ae0a3'
QTI = 'dd3c9a9a2a136f649fa8ece063aea2ac0e581ec0'

jobs = []
def archive(repo, subdir, dest):
    jobs.append((f'https://android.googlesource.com/platform/{repo}/+archive/{TAG}/{subdir}.tar.gz', dest, 'tar'))

for repo, subdir in [
    ('system/libhidl', 'base/include'), ('system/libhidl', 'transport/include'),
    ('system/libhwbinder', 'include'), ('system/core', 'libutils/include'),
    ('system/core', 'libcutils/include'), ('system/libbase', 'include'),
    ('system/logging', 'liblog/include'), ('system/core', 'libsystem/include'),
    ('system/libfmq', 'include'), ('system/libfmq', 'base'),
]:
    archive(repo, subdir, 'include')
for kind, versions in [('common', ['1.0','1.1','1.2']), ('mapper', ['2.0','2.1','3.0','4.0'])]:
    for version in versions:
        archive('hardware/interfaces', f'graphics/{kind}/{version}', f'hal/android/hardware/graphics/{kind}/{version}')
archive('system/libhidl', 'transport/base/1.0', 'hal/android/hidl/base/1.0')
for version in ['1.0','1.1','1.2']:
    archive('system/libhidl', f'transport/manager/{version}', f'hal/android/hidl/manager/{version}')
for path, dest in [('linux-x86/bin/hidl-gen','hidl-gen'), ('linux-x86/lib64/libc++.so','libc++.so')]:
    jobs.append((f'https://android.googlesource.com/platform/prebuilts/build-tools/+/{TOOL}/{path}?format=TEXT', dest, 'b64'))
for path in ['mapper/4.0/IQtiMapper.hal', 'mapperextensions/1.0/IQtiMapperExtensions.hal',
             'mapperextensions/1.0/types.hal', 'mapperextensions/1.1/IQtiMapperExtensions.hal']:
    jobs.append((f'https://raw.githubusercontent.com/LineageOS/android_vendor_qcom_opensource_interfaces/{QTI}/display/{path}',
                 f'hal/vendor/qti/hardware/display/{path}', 'raw'))

def fetch(job):
    url, dest, kind = job
    cache = ROOT / 'downloads' / hashlib.sha256(url.encode()).hexdigest()
    cache.parent.mkdir(exist_ok=True)
    if cache.exists():
        data = cache.read_bytes()
    else:
        print(f'Fetching {url}', flush=True)
        # Some WSL lab networks cannot reach GitHub directly; the host curl
        # retains its normal network configuration and TLS verification.
        host_curl = shutil.which('curl.exe')
        if host_curl and url.startswith('https://raw.githubusercontent.com/'):
            data = subprocess.check_output([host_curl, '-fsSL', '--max-time', '30', url])
        else:
            with urlopen(url, timeout=30) as response:
                data = response.read()
        cache.write_bytes(data)
    if kind == 'b64':
        data = base64.b64decode(data, validate=False)
    digest = hashlib.sha256(data).hexdigest()
    if expected.get(url) != digest:
        raise RuntimeError(f'Public input hash mismatch: {url}')
    target = ROOT / dest
    if kind == 'tar':
        target.mkdir(parents=True, exist_ok=True)
        with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as tar:
            tar.extractall(target, filter='data')
    else:
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    return {'url':url, 'destination':dest, 'decoded_sha256':digest}

with ThreadPoolExecutor(max_workers=6) as pool:
    results = list(pool.map(fetch, jobs))
(ROOT / 'hidl-gen').chmod(0o755)
(ROOT / 'sources.json').write_text(json.dumps(results, indent=2) + '\n')
print(f'Fetched {len(results)} public inputs to {ROOT}')
