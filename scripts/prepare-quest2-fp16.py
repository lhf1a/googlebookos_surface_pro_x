#!/usr/bin/env python3
"""Stage combined-v3 by replacing only v1's private mapper adapter.

Version 2 was an abandoned private probe with preemptible C++ layout helpers.
Version 3 hides those helpers so the unchanged v4 AHB adapter keeps its own ABI.
"""
import importlib.util
from pathlib import Path
import shutil
import hashlib
import subprocess
ROOT=Path(__file__).resolve().parents[1]
OUTPUT=ROOT/'out/quest2-gpu/combined-v3'
MAPPER_SHA='e9eaf75316eb4285bf8e8f79acefbabb72790232baa353eccd6cee9cf44884dc'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    spec=importlib.util.spec_from_file_location('combined',ROOT/'scripts/prepare-quest2-combined.py')
    base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
    expected=base.VK|base.GL;source=ROOT/'out/quest2-gpu/combined-v1'
    mapper=ROOT/'out/quest2-gpu/fp16-probe/libqti-mapper3-fp16-hidden.so'
    if OUTPUT.exists():raise SystemExit('Refusing existing output')
    for name,digest in expected.items():
        p=source/name
        if p.is_symlink() or not p.is_file() or sha(p)!=digest:raise SystemExit('Input mismatch: '+name)
    if mapper.is_symlink() or sha(mapper)!=MAPPER_SHA:raise SystemExit('Mapper mismatch')
    symbols=subprocess.check_output(['readelf','--dyn-syms','-W',str(mapper)],text=True)
    if any('_ZN3sq2' in line and ('color_info' in line or '7mapper5' in line or '8Metadata' in line)
           for line in symbols.splitlines()):
        raise SystemExit('Private C++ metadata helper leaked into dynamic symbol ABI')
    OUTPUT.mkdir()
    for name in expected:shutil.copy2(source/name,OUTPUT/name)
    shutil.copy2(mapper,OUTPUT/'libqti-mapper3-bridge.so');expected['libqti-mapper3-bridge.so']=MAPPER_SHA
    for name,digest in expected.items():
        if sha(OUTPUT/name)!=digest:raise SystemExit('Output mismatch')
    (OUTPUT/'SHA256SUMS').write_text(''.join(f'{v}  {k}\n' for k,v in sorted(expected.items())))
    print('QUEST2_FP16_PREPARED libraries=14 changed_libraries=1')
if __name__=='__main__':main()
