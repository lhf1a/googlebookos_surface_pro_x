#!/usr/bin/env python3
"""Stage combined-v5 with strict RGBX/BGRA mapper metadata support."""
import importlib.util
from pathlib import Path
import shutil
import hashlib
import subprocess
ROOT=Path(__file__).resolve().parents[1]
OUTPUT=ROOT/'out/quest2-gpu/combined-v5'
MAPPER_SHA='dc0f8d0530154501fa849408d115fb22c14732b4e18a38977a01c791bbb3a0ac'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    spec=importlib.util.spec_from_file_location('combined',ROOT/'scripts/prepare-quest2-combined.py')
    base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
    expected=base.VK|base.GL
    expected['libqti-mapper3-bridge.so']='e9eaf75316eb4285bf8e8f79acefbabb72790232baa353eccd6cee9cf44884dc'
    del expected['libllvm-glnext.so'];del expected['libllvm-qgl.so']
    expected.update({'libGLESv2_adreno.so':'d7e41532779ba5be94eae207ce3ad6800489a93ba16ccc1fe7b80aa9778995ec',
        'libq2v77glnext.so':'17a9cb625c816bf9560489b53e15f20ad356db04ee0f1e83e5fbac03cb0fe3f3',
        'libq2v77qgl.so':'100a97a7c9cfa4b6d554cc1a8960a920de963aeba3b6c3a6bc2ea8d986472652',
        'vulkan-v77-bridge.so':'e18b5adf7d213c2058c3ef93028ed6db1af8aa83e398f07681545bff9382dd54'})
    source=ROOT/'out/quest2-gpu/combined-v4'
    mapper=ROOT/'out/quest2-gpu/color-probe/libqti-mapper3-color.so'
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
    print('QUEST2_COLOR_PREPARED libraries=14 changed_libraries=1')
if __name__=='__main__':main()
