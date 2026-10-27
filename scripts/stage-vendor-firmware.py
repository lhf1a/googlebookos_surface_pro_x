#!/usr/bin/env python3
"""Stage verified Windows firmware inputs; no patching, network or device access."""
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import shutil

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('collection', type=Path)
parser.add_argument('output', type=Path)
parser.add_argument('--gpu-package', required=True, help='Directory matching the active Windows GPU driver')
args = parser.parse_args()
collection = args.collection.resolve()
output = args.output.resolve()
if output.exists():
    raise SystemExit('Output already exists; choose a new directory.')
manifest = json.loads((collection / 'firmware-manifest.json').read_text(encoding='utf-8-sig'))
if manifest['firmwareSkipped'] or manifest['missingPackages']:
    raise SystemExit('Incomplete firmware collection.')
verified = {}
for item in manifest['files']:
    path = (collection / item['path'].replace('\\', '/')).resolve()
    if not path.is_relative_to(collection) or not path.is_file():
        raise SystemExit(f'Invalid manifest path: {item["path"]}')
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if path.stat().st_size != item['bytes'] or digest != item['sha256']:
        raise SystemExit(f'Collection hash mismatch: {path.name}')
    verified[path] = digest
upstream = runpy.run_path(str(ROOT / 'upstream/aarch64-firmware/scripts/getfw.py'))
driver_store = collection / 'windows-root/Windows/System32/DriverStore/FileRepository'
planned = []
for source in upstream['sources']:
    if not isinstance(source, upstream['WindowsDriverFirmware']):
        continue
    candidates = sorted(p for p in driver_store.iterdir() if p.name.startswith(source.source_directory))
    if source.source_directory == 'qcdx8180':
        candidates = [p for p in candidates if p.name == args.gpu_package]
    if len(candidates) != 1:
        raise SystemExit(f'Ambiguous or missing package: {source.source_directory}')
    for original, target in source.files.items():
        src = (candidates[0] / original).resolve()
        dest = output / source.target_directory / target
        if src not in verified or not dest.resolve().is_relative_to(output):
            raise SystemExit(f'Unverified input or invalid target: {src}')
        planned.append((src, dest, source.name))
output.mkdir(parents=True)
provenance = []
for src, dest, group in planned:
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(src, dest)
    digest = hashlib.sha256(dest.read_bytes()).hexdigest()
    if digest != verified[src]:
        raise SystemExit(f'Copy verification failed: {dest}')
    provenance.append({'group': group, 'source': src.relative_to(collection).as_posix(),
                       'output': dest.relative_to(output).as_posix(), 'sha256': digest})
(output / 'provenance.json').write_text(json.dumps({'status': 'Unpatched Windows firmware inputs; not ready for Linux installation',
                                                  'files': provenance}, indent=2) + '\n')
print(f'Staged and verified {len(provenance)} vendor firmware inputs in {output}')
