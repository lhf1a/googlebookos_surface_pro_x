#!/usr/bin/env python3
"""Defer the v77 shared counter-node release until the queue loop is complete.

No callback results, queue requests, allocator choice or successful-list ownership
are fabricated. Only pinned donor bytes are patched. Private candidate only.
"""
import hashlib
import importlib.util
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
STUB=0x2d4f60
LIB='vulkan-v77-bridge.so'

def branch(a,b):
    delta=b-a
    if delta%4 or not -(1<<27)<=delta<(1<<27):raise ValueError('branch range')
    return 0x14000000|((delta//4)&0x3ffffff)

def cbz(a,b,reg):
    delta=b-a
    if delta%4 or not -(1<<20)<=delta<(1<<20):raise ValueError('cbz range')
    return 0x34000000|(((delta//4)&0x7ffff)<<5)|reg

def require(condition,message):
    if not condition:raise ValueError(message)

def patch(original):
    data=bytearray(original)
    words={0xd2410:0xb94052cc,0xd2434:0x17ffffe7,0xd295c:0x3400391c}
    for at,word in words.items():require(struct.unpack_from('<I',data,at)[0]==word,'Unexpected site')
    # On a failed record restore the preceding success flag and continue;
    # on final all-failed outcome enter the ORIGINAL allocator-specific frees.
    stub=struct.pack('<6I',0x2a1403fc,branch(STUB+4,0xd23d0),
                     cbz(STUB+8,STUB+20,28)|0x01000000,
                     0xb94052cc,branch(STUB+16,0xd2414),branch(STUB+20,0xd2960))
    phoff=struct.unpack_from('<Q',data,32)[0]
    phsize,count=struct.unpack_from('<HH',data,54)
    require(phsize==56 and count==13,'Unexpected program headers')
    headers=[struct.unpack_from('<IIQQQQQQ',data,phoff+i*56) for i in range(count)]
    expected=(1,5,0xbd000,0xbd000,0xbd000,0x217f60,0x217f60,0x1000)
    require(headers.count(expected)==1,'Unexpected RX segment')
    require(not any(data[STUB:0x2d5000]),'Nonempty RX tail')
    def overlap(a,n,b,m):return a<b+m and b<a+n
    for p in headers:
        require(not overlap(p[2],p[5],STUB,len(stub)),'File segment overlap')
        if p[0]==1:require(not overlap(p[3],p[6],STUB,len(stub)),'Memory segment overlap')
    shoff=struct.unpack_from('<Q',data,40)[0]
    shsize,shcount=struct.unpack_from('<HH',data,58)
    require(shsize==64 and shoff+shcount*shsize<=len(data),'Section headers')
    for i in range(shcount):
        s=struct.unpack_from('<IIQQQQIIQQ',data,shoff+i*64)
        if s[1]!=8:require(not overlap(s[4],s[5],STUB,len(stub)),'Section overlap')
    replacements={0xd2410:branch(0xd2410,STUB),
                  0xd2434:branch(0xd2434,0xd307c),
                  0xd295c:branch(0xd295c,STUB+8)}
    for at,word in replacements.items():struct.pack_into('<I',data,at,word)
    data[STUB:STUB+len(stub)]=stub
    at=phoff+headers.index(expected)*56+32
    struct.pack_into('<QQ',data,at,STUB+len(stub)-0xbd000,STUB+len(stub)-0xbd000)
    allowed=set(range(STUB,STUB+len(stub)))|set(range(at,at+16))
    for p in replacements:allowed.update(range(p,p+4))
    require(len(data)==len(original) and all(a==b or i in allowed for i,(a,b) in enumerate(zip(original,data))),'Unexpected byte changes')
    return data

def main():
    spec=importlib.util.spec_from_file_location('vkonly',ROOT/'scripts/prepare-quest2-vulkan-only.py')
    base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
    source=ROOT/'out/quest2-gpu/vulkan-only-v1'
    output=ROOT/'out/quest2-gpu/vulkan-only-counter-v1'
    require(not output.exists(),'Refusing existing output')
    for name,digest in base.EXPECTED.items():
        p=source/name
        require(not p.is_symlink() and hashlib.sha256(p.read_bytes()).hexdigest()==digest,'Input mismatch: '+name)
    data=patch((source/LIB).read_bytes())
    with tempfile.TemporaryDirectory(prefix='sq2-counter-cleanup-') as tmp:
        tmp=Path(tmp);(tmp/'candidate.so').write_bytes(data)
        subprocess.run(['aarch64-linux-gnu-gcc','-static','-O2','-Wall','-Wextra','-Werror',
                        str(ROOT/'probe/graphics/quest2-counter-cleanup-test.c'),
                        str(ROOT/'probe/graphics/quest2-counter-cleanup-test.S'),'-o',str(tmp/'check')],check=True)
        result=subprocess.check_output(['qemu-aarch64',str(tmp/'check'),str(tmp/'candidate.so')],text=True,timeout=60).strip()
        require(result=='COUNTER_CLEANUP_QEMU_PASS cases=680','Offline ownership test failed')
    output.mkdir()
    for name in base.EXPECTED:shutil.copy2(source/name,output/name)
    (output/LIB).write_bytes(data)
    hashes=dict(base.EXPECTED);hashes[LIB]=hashlib.sha256(data).hexdigest()
    (output/'SHA256SUMS').write_text(''.join(f'{h}  {n}\n' for n,h in sorted(hashes.items())))
    print(result);print('VULKAN_COUNTER_CLEANUP_PREPARED',hashes[LIB])

if __name__=='__main__':main()
