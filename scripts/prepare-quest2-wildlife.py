#!/usr/bin/env python3
"""Prepare private test binaries with vendor-local dependency search paths."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
manifest = json.loads((root / 'configs/quest2-gpu-research.json').read_text())
source = root / 'out/quest2-gpu/runtime'
dest = root / 'out/quest2-wild-life/runtime'
dest.mkdir(parents=True, exist_ok=True)
for name, expected in manifest['runtime_artifact_sha256'].items():
    original = source / name
    if hashlib.sha256(original.read_bytes()).hexdigest() != expected:
        raise SystemExit(f'Unverified runtime: {name}')
    target = dest / name
    shutil.copyfile(original, target)
    subprocess.run(['patchelf', '--set-rpath', '$ORIGIN', str(target)], check=True)
lines = [f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}'
         for p in sorted(dest.glob('*.so'))]
(dest / 'SHA256SUMS').write_text('\n'.join(lines) + '\n')
print('WILD_LIFE_PRIVATE_RUNTIME_READY')
