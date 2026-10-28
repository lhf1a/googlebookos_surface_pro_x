#!/usr/bin/env python3
"""Give matching v77 compiler libraries unique filenames/SONAMEs.

Equal-length data-string edits only; all executable bytes and ELF layouts stay
fixed. This prevents Android app namespaces resolving the stock vendor LLVM
libraries with identical old names. No device writes or system installation.
"""
import hashlib
import importlib.util
from pathlib import Path
import struct
import json
ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'out/quest2-gpu/combined-v3'
OUTPUT=ROOT/'out/quest2-gpu/combined-v4'
RENAMES={'libllvm-glnext.so':'libq2v77glnext.so','libllvm-qgl.so':'libq2v77qgl.so'}
COUNTS={'libGLESv2_adreno.so':{'libllvm-glnext.so':1},
 'vulkan-v77-bridge.so':{'libllvm-glnext.so':1},
 'libllvm-glnext.so':{'libllvm-glnext.so':2,'libllvm-qgl.so':1},
 'libllvm-qgl.so':{'libllvm-qgl.so':2}}
def sha(data):return hashlib.sha256(data).hexdigest()
def main():
    if OUTPUT.exists():raise SystemExit('Refusing existing output')
    spec=importlib.util.spec_from_file_location('combined',ROOT/'scripts/prepare-quest2-combined.py')
    prior=importlib.util.module_from_spec(spec);spec.loader.exec_module(prior)
    expected=prior.VK|prior.GL
    expected['libqti-mapper3-bridge.so']='e9eaf75316eb4285bf8e8f79acefbabb72790232baa353eccd6cee9cf44884dc'
    prepared={};reports={}
    for name,digest in expected.items():
        p=SOURCE/name
        if p.is_symlink() or not p.is_file() or sha(p.read_bytes())!=digest:raise SystemExit('Input mismatch: '+name)
        original=p.read_bytes();data=bytearray(original);allowed=set();edits=[]
        for old,new in RENAMES.items():
            before=old.encode()+b'\0';after=new.encode()+b'\0'
            if len(before)!=len(after):raise SystemExit('Rename changes string size')
            count=COUNTS.get(name,{}).get(old,0)
            if original.count(before)!=count:raise SystemExit('Unexpected filename count: '+name+'/'+old)
            start=0
            for _ in range(count):
                at=original.index(before,start);data[at:at+len(before)]=after
                allowed.update(range(at,at+len(before)));edits.append({'offset':hex(at),'old':old,'new':new});start=at+len(before)
        if any(a!=b and i not in allowed for i,(a,b) in enumerate(zip(original,data))):raise SystemExit('Unexpected byte change')
        # No instructions, symbol/relocation layout, RX padding stubs, or ELF
        # program headers are repacked. A filename inside executable code is
        # explicitly refused rather than being assumed harmless.
        shoff=struct.unpack_from('<Q',original,40)[0];size,count=struct.unpack_from('<HH',original,58)
        if size!=64 or shoff+size*count>len(original):raise SystemExit('Bad ELF section table')
        for i in range(count):
            section=struct.unpack_from('<IIQQQQIIQQ',original,shoff+i*size)
            if section[2]&4 and original[section[4]:section[4]+section[5]]!=data[section[4]:section[4]+section[5]]:
                raise SystemExit('Executable section changed: '+name)
        output_name=RENAMES.get(name,name);prepared[output_name]=bytes(data)
        reports[output_name]={'input_sha256':digest,'output_sha256':sha(data),'edits':edits}
    OUTPUT.mkdir()
    for name,data in prepared.items():(OUTPUT/name).write_bytes(data)
    (OUTPUT/'SHA256SUMS').write_text(''.join(f'{sha(v)}  {k}\n' for k,v in sorted(prepared.items())))
    (OUTPUT/'private-compiler.json').write_text(json.dumps({'status':'offline_prepared_not_device_tested',
        'library_count':len(prepared),'unchanged_elf_layout':True,'files':reports},indent=2)+'\n')
    print('QUEST2_PRIVATE_COMPILER_PREPARED libraries='+str(len(prepared)))
if __name__=='__main__':main()
