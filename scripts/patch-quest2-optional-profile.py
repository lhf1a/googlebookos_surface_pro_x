#!/usr/bin/env python3
"""Isolated v819 donor experiment: skip mismatched optional profile callback.

The inspected call site guards a null dlsym result. Change only the symbol
lookup name, never instructions, chip IDs, or system libraries.
"""
import argparse
import hashlib
import json
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('source', type=Path)
parser.add_argument('dest', type=Path)
args = parser.parse_args()
source, dest = args.source, args.dest
if source.resolve() == dest.resolve():
    parser.error('Use a distinct output; preserve the original donor')
data = source.read_bytes()
root = Path(__file__).resolve().parents[1]
expected = json.loads((root / 'configs/quest2-gpu-research.json').read_text(encoding='utf-8'))
donor = next(d for d in expected['donor_userspace'] if '/v819.1' in d['release'])
sha = next(f['sha256'] for f in donor['files'] if f['path'] == 'vulkan.adreno.so')
if hashlib.sha256(data).hexdigest() != sha:
    parser.error('Unexpected donor SHA256')
old = b'ApplyApplicationProfile\0'
new = b'Skip_ApplicationProfile\0'
if len(old) != len(new) or data.count(old) != 1:
    parser.error('Unexpected optional profile symbol layout')
dest.parent.mkdir(parents=True, exist_ok=True)
dest.write_bytes(data.replace(old, new))
print(hashlib.sha256(dest.read_bytes()).hexdigest())
