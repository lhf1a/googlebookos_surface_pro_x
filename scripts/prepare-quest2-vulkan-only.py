#!/usr/bin/env python3
"""Copy pinned Vulkan dependencies only; never stage donor EGL/GLES entry points."""
import hashlib
from pathlib import Path
import shutil
ROOT=Path(__file__).resolve().parents[1]
EXPECTED={'libadreno_utils.so': '6ef886f4b2d0d648e9ed3f6ee803ac70a2375421f90004bc6800fe8e229244ba',
 'libgsl.so': 'f16bc42ec0836dfadd28d5825158ab56617deaea50fc90937e984766af90d108',
 'libq2v77glnext.so': '17a9cb625c816bf9560489b53e15f20ad356db04ee0f1e83e5fbac03cb0fe3f3',
 'libq2v77qgl.so': '100a97a7c9cfa4b6d554cc1a8960a920de963aeba3b6c3a6bc2ea8d986472652',
 'libqti-mapper3-bridge.so': 'dc0f8d0530154501fa849408d115fb22c14732b4e18a38977a01c791bbb3a0ac',
 'libquest2-ion-dmaheap.so': '56ce5b61ba12b966902abcddd51530ca12fa18b60f5e34bc6ef582e79eb631b8',
 'libquest2-v77-ahb.so': '6f7562d6f9503067f7de7eb094591145134ef43682b0bdad833e4e50baeb49a5',
 'libquest2-vulkan-wrapper.so': '1cf8564084cbce59937ba512cd58f26a44bd148c80d306336b1fcb4db1e0ebd7',
 'vulkan-v77-bridge.so': 'e18b5adf7d213c2058c3ef93028ed6db1af8aa83e398f07681545bff9382dd54'}
def main():
    source=ROOT/'out/quest2-gpu/combined-v5'
    output=ROOT/'out/quest2-gpu/vulkan-only-v1'
    if output.exists():raise SystemExit('Refusing existing output')
    for name,expected in EXPECTED.items():
        p=source/name
        if p.is_symlink() or hashlib.sha256(p.read_bytes()).hexdigest()!=expected:
            raise SystemExit('Input mismatch: '+name)
    output.mkdir()
    for name in EXPECTED:shutil.copy2(source/name,output/name)
    (output/'SHA256SUMS').write_text(''.join(f'{h}  {n}\n' for n,h in sorted(EXPECTED.items())))
    print('VULKAN_ONLY_PREPARED libraries=9 egl_gles_donors=0')
if __name__=='__main__':main()
