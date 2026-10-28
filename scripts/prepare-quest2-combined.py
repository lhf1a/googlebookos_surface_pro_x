#!/usr/bin/env python3
"""Copy the audited matching v77 GLES/Vulkan set; no ELF rewriting/device I/O."""
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT/'out/quest2-gpu/combined-v1'
VK = {
'libadreno_utils.so':'6ef886f4b2d0d648e9ed3f6ee803ac70a2375421f90004bc6800fe8e229244ba',
'libgsl.so':'f16bc42ec0836dfadd28d5825158ab56617deaea50fc90937e984766af90d108',
'libllvm-glnext.so':'6d0d501eef2418ff64659f06909cf9990c190ac328b9b2a4281a343972061a7d',
'libllvm-qgl.so':'3c451fd930e1f56c17cb136d75e6514089134f713c011a746a62b8ccd2488d8a',
'libqti-mapper3-bridge.so':'ade19efb787641daf1305c1d5787e93cc29029fd1462074455ff3a616979ab75',
'libquest2-ion-dmaheap.so':'56ce5b61ba12b966902abcddd51530ca12fa18b60f5e34bc6ef582e79eb631b8',
'libquest2-v77-ahb.so':'6f7562d6f9503067f7de7eb094591145134ef43682b0bdad833e4e50baeb49a5',
'libquest2-vulkan-wrapper.so':'1cf8564084cbce59937ba512cd58f26a44bd148c80d306336b1fcb4db1e0ebd7',
'vulkan-v77-bridge.so':'b087293a0a59ce4d4a3ed25dfc29179a920c2fcab31587de3f8489fb481f3cbb'}
GL = {
'libEGL_adreno.so':'026b25fe82d174fe9e4a327d4e6f9b05be800a59853b5cb729cae53a6b2892fc',
'libGLESv1_CM_adreno.so':'396987f605b4f7aa1be5d10cacf0216bce400a3c13bb36f9cf29fac1d9bc5ace',
'libGLESv2_adreno.so':'1d3ba33ea006962766f631362556ffa1b1e05835ceb733285bfbe37e580cce77',
'eglSubDriverAndroid.so':'eaa30f3fa6ed9dcf79ad12c9c214aab9d11aebd2cf6d8b3388e8d0075ef17012',
'libquest2-egl-wrapper.so':'68712421fe8b68de52d39381d678eac2abf4324328fc7d8127894b74cedab389'}
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    if OUTPUT.exists(): raise SystemExit('Refusing existing output')
    sources={}
    for directory,values in [('v77-vulkan-ahb-v4',VK),('gles-linked-fence-prune',GL)]:
        for name,value in values.items():
            path=ROOT/'out/quest2-gpu'/directory/name
            if path.is_symlink() or not path.is_file() or sha(path)!=value:
                raise SystemExit('Input mismatch: '+name)
            sources[name]=path
    OUTPUT.mkdir()
    for name,path in sources.items():
        shutil.copy2(path,OUTPUT/name)
        if sha(OUTPUT/name)!=(VK|GL)[name]: raise SystemExit('Copy mismatch: '+name)
    (OUTPUT/'SHA256SUMS').write_text(''.join(f'{v}  {k}\n' for k,v in sorted((VK|GL).items())))
    (OUTPUT/'combined.json').write_text(json.dumps({'status':'prepared_not_device_tested',
        'libraries':14,'sha256':VK|GL,'elf_bytes_unchanged':True},indent=2)+'\n')
    print('QUEST2_COMBINED_PREPARED libraries=14')
if __name__=='__main__': main()
