#!/usr/bin/env python3
"""Prepare an isolated v819 runtime; never install to the system vendor tree."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / 'out/quest2-gpu/runtime'
out.mkdir(parents=True, exist_ok=True)
manifest = json.loads((root / 'configs/quest2-gpu-research.json').read_text())
donor = next(d for d in manifest['donor_userspace'] if '/v819.1' in d['release'])
source = root / 'downloads/quest2-gpu/v819'
for item in donor['files']:
    path = source / item['path']
    if hashlib.sha256(path.read_bytes()).hexdigest() != item['sha256']:
        raise SystemExit(f'Donor hash mismatch: {item["path"]}')
    if item['path'] != 'vulkan.adreno.so':
        shutil.copyfile(path, out / path.name)
subprocess.run([sys.executable, str(root / 'scripts/patch-quest2-optional-profile.py'),
                str(source / 'vulkan.adreno.so'), str(out / 'vulkan-bridge.so')], check=True)
shutil.copyfile(root / 'out/quest2-gpu/libqti-mapper-bridge.so', out / 'libqti-mapper-bridge.so')
subprocess.run(['patchelf', '--set-soname', 'vulkan-bridge.so', '--add-needed',
                'libqti-mapper-bridge.so', str(out / 'vulkan-bridge.so')], check=True)
hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(out.glob('*.so'))}
(out / 'hashes.json').write_text(json.dumps(hashes, indent=2) + '\n')
print('QUEST2_ISOLATED_RUNTIME_PREPARED')
