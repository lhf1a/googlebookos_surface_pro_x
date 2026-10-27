#!/usr/bin/env python3
"""Read-only, checksum-validated GPT/LP extraction for the SQ2 boot experiment."""
from pathlib import Path
import gzip,hashlib,json,re,struct,subprocess,sys,importlib.util
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('inspect_recovery',ROOT/'scripts/inspect-recovery.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
inspect=module.inspect
raw=ROOT/'downloads/mica-recovery.raw'; out=ROOT/'out/googlebook-stage'
out.mkdir(exist_ok=True)
layout=inspect(raw); parts={p['name']:p for p in layout['partitions']}
u=lambda b,o:struct.unpack_from('<I',b,o)[0]
align=lambda n,p:(n+p-1)//p*p
def read(off,size):
    assert 0<=off and off+size<=raw.stat().st_size
    with raw.open('rb') as f:f.seek(off); data=f.read(size)
    assert len(data)==size
    return data
def save(name,data):
    (out/name).write_bytes(data)
def decompress(data):
    return gzip.decompress(data) if data[:2]==b'\x1f\x8b' else subprocess.check_output(['lz4','-d','-c'],input=data)
for name in ['boot_a','init_boot_a','vendor_boot_a']:
    p=parts[name]; b=read(p['offset'],p['bytes'])
    if name=='boot_a':
        kernel=b[4096:4096+u(b,8)]
        if kernel[56:60]!=b'ARM\x64':kernel=decompress(kernel)
        assert kernel[56:60]==b'ARM\x64'
        save('original-kernel.Image',kernel)
    elif name=='init_boot_a':save('init.cpio',decompress(b[4096:4096+u(b,12)]))
    else:
        page=u(b,12); start=align(u(b,2096),page)
        table=start+align(u(b,24),page)+align(u(b,2100),page)
        ts,count,es,bs=struct.unpack_from('<IIII',b,2112)
        for i in range(count):
            size,off,kind=struct.unpack_from('<III',b,table+i*es)
            save(f'vendor-ramdisk-{i}-type-{kind}.cpio',decompress(b[start+off:start+off+size]))
        save('original-bootconfig.txt',b[table+align(ts,page):table+align(ts,page)+bs])
base=parts['super']['offset']; total=parts['super']['bytes']
g=read(base+4096,4096); assert u(g,0)==0x616c4467
gs=u(g,4); checked=bytearray(g[:gs]);checked[8:40]=bytes(32)
assert hashlib.sha256(checked).digest()==g[8:40]
meta=read(base+12288,u(g,40)); assert u(meta,0)==0x414c5030
hs=u(meta,8); h=bytearray(meta[:hs]);h[12:44]=bytes(32)
assert hashlib.sha256(h).digest()==meta[12:44]
t=meta[hs:hs+u(meta,44)]; assert hashlib.sha256(t).digest()==meta[48:80]
po,pn,ps=struct.unpack_from('<III',meta,80);eo,en,es=struct.unpack_from('<III',meta,92)
assert ps>=52 and es>=24
logical=[]
for i in range(pn):
    p=t[po+i*ps:po+(i+1)*ps];name=p[:36].split(b'\0')[0].decode()
    assert re.fullmatch('[A-Za-z0-9_]+',name)
    first,count=struct.unpack_from('<II',p,40); assert first+count<=en
    extents=[]
    for j in range(first,first+count):
        sectors,kind,sector,device=struct.unpack_from('<QIQI',t,eo+j*es)
        assert kind in (0,1) and device==0
        if kind==0:assert sector*512+sectors*512<=total
        extents.append(dict(bytes=sectors*512,kind=kind,offset=base+sector*512))
    entry=dict(name=name,bytes=sum(x['bytes'] for x in extents),extents=extents)
    logical.append(entry)
    if not name.endswith('_a') or not entry['bytes']:continue
    target=out/(name+'.img')
    digest=hashlib.sha256()
    with raw.open('rb') as src,target.open('wb') as dst:
        for e in extents:
            left=e['bytes'];src.seek(e['offset'])
            while left:
                n=min(left,8*1024*1024);chunk=src.read(n) if e['kind']==0 else bytes(n)
                assert len(chunk)==n;dst.write(chunk);digest.update(chunk);left-=n
    entry['sha256']=digest.hexdigest()
    print(name,entry['bytes'],entry['sha256'],flush=True)
(out/'logical-partitions.json').write_text(json.dumps(logical,indent=2)+'\n')
sys.path.insert(0,str(ROOT/'upstream/gbos-vm/image'))
from cpio_tools import read as cpio_read
entries=cpio_read((out/'init.cpio').read_bytes())
save('original-init',entries['init'][1])
(out/'ramdisk-list.txt').write_text('\n'.join(entries)+'\n')
print('PASS: validated GPT and LP header/table hashes; original recovery remains read-only')
