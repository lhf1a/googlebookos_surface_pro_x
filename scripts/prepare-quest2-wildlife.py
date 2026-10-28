#!/usr/bin/env python3
"""Prepare private test binaries with vendor-local dependency search paths."""
import hashlib
import argparse
import json
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--direct',action='store_true',help='Use the pinned private Direct candidate')
args=parser.parse_args()
manifest = json.loads((root / 'configs/quest2-gpu-research.json').read_text())
source = root / ('out/quest2-gpu/vulkan-direct-runtime' if args.direct else 'out/quest2-gpu/runtime')
dest = root / ('out/quest2-wild-life/direct/runtime' if args.direct else 'out/quest2-wild-life/runtime')
expected_files=dict(manifest['runtime_artifact_sha256'])
if args.direct:
    expected_files['vulkan-bridge.so']='3e01f9acaab3dc6eaf4239da317b78b4a4514a91cbf5d129b45b5e0c94897b27'
    if (source/'qgl_config.txt').read_bytes()!=b'0x0=0x08675309\nRenderMode=Direct\n':
        raise SystemExit('Unexpected Direct configuration')
for name, expected in expected_files.items():
    if hashlib.sha256((source/name).read_bytes()).hexdigest()!=expected:
        raise SystemExit(f'Unverified runtime: {name}')
dest.mkdir(parents=True, exist_ok=True)
for name, expected in expected_files.items():
    original = source / name
    if hashlib.sha256(original.read_bytes()).hexdigest() != expected:
        raise SystemExit(f'Unverified runtime: {name}')
    target = dest / name
    shutil.copyfile(original, target)
    subprocess.run(['patchelf', '--set-rpath', '$ORIGIN', str(target)], check=True)
lines = [f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}'
         for p in sorted(dest.glob('*.so'))]
(dest / 'SHA256SUMS').write_text('\n'.join(lines) + '\n')
if args.direct:
    shutil.copyfile(source/'qgl_config.txt',dest/'qgl_config.txt')
    with (dest/'SHA256SUMS').open('a') as out:
        out.write(hashlib.sha256((dest/'qgl_config.txt').read_bytes()).hexdigest()+'  qgl_config.txt\n')
print('WILD_LIFE_PRIVATE_RUNTIME_READY')
