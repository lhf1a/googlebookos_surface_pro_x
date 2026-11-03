#!/usr/bin/env python3
"""Bound only DesktopPowerService's AC screen-off grace; never edits userdata."""
import argparse, hashlib, json, struct, zipfile, zlib, shutil
from pathlib import Path
CLASS = 'Lcom/android/server/desktop/power/DesktopPowerService$screenStateReceiver$1;'
NEW_MS = 1000

def uleb(d, p):
    v = shift = 0
    for _ in range(5):
        b = d[p]; p += 1; v |= (b & 127) << shift
        if b < 128: return v, p
        shift += 7
    raise ValueError('bad uleb')

def find_call(d):
    u16=lambda p:struct.unpack_from('<H',d,p)[0]
    u32=lambda p:struct.unpack_from('<I',d,p)[0]
    strings=[]
    for i in range(u32(0x38)):
        p=u32(u32(0x3c)+i*4);_,p=uleb(d,p)
        strings.append(bytes(d[p:d.index(0,p)]).decode('utf-8',errors='replace'))
    types=[strings[u32(u32(0x44)+i*4)] for i in range(u32(0x40))]
    methods=[]
    for i in range(u32(0x58)):
        p=u32(0x5c)+i*8
        methods.append((types[u16(p)],strings[u32(p+4)]))
    for i in range(u32(0x60)):
        c=u32(0x64)+i*32
        if types[u32(c)]!=CLASS: continue
        p=u32(c+24);counts=[]
        for _ in range(4): n,p=uleb(d,p);counts.append(n)
        for _ in range(counts[0]+counts[1]):
            _,p=uleb(d,p);_,p=uleb(d,p)
        for count in counts[2:]:
            mid=0
            for _ in range(count):
                delta,p=uleb(d,p);mid+=delta
                _,p=uleb(d,p);code,p=uleb(d,p)
                if methods[mid]!=(CLASS,'onReceive'): continue
                begin=code+16;end=begin+u32(code+12)*2
                hits=[]
                for k in range(begin,end-10,2):
                    op=d[k]
                    if op not in (0x16,0x17,0x18): continue
                    size={0x16:2,0x17:4,0x18:8}[op]
                    nxt=k+2+size
                    if d[nxt]!=0x6e: continue
                    target=methods[u16(nxt+2)]
                    value=int.from_bytes(d[k+2:nxt],'little',signed=True)
                    if target==('Landroid/os/PowerManager$WakeLock;','acquire') and 0 <= value <= 2147483647:
                        hits.append(dict(offset=k+2,size=size,timeout_ms=value,method='onReceive',class_name=CLASS))
                if len(hits)!=1: raise ValueError('ambiguous exact WakeLock.acquire constant: '+repr(hits))
                return hits[0]
        raise ValueError('onReceive code missing')
    return None

def inspect(path):
    found=[]
    with zipfile.ZipFile(path) as z:
        for name in z.namelist():
            if name.endswith('.dex'):
                d=bytearray(z.read(name));item=find_call(d)
                if item: found.append(dict(member=name,**item))
    if len(found)!=1: raise ValueError('exact class/method missing or ambiguous')
    return found[0]

def patch(source, output):
    item=inspect(source)
    original_sha = hashlib.sha256(source.read_bytes()).hexdigest()
    original_ms = item['timeout_ms']
    if output.exists(): raise ValueError('fresh output required')
    if original_ms <= NEW_MS:
        shutil.copyfile(source,output)
        return dict(unchanged=True,reason='existing grace is already at most 1000ms',
                    baseline_timeout_ms=original_ms,modified_timeout_ms=original_ms,
                    original_sha256=original_sha,modified_sha256=original_sha)
    with zipfile.ZipFile(source) as a,zipfile.ZipFile(output,'w') as b:
        b.comment=a.comment
        for info in a.infolist():
            if info.filename.upper().startswith('META-INF/') and info.filename.upper().endswith(('.RSA','.DSA','.EC')):
                raise ValueError('refuse signed JAR mutation')
            data=a.read(info.filename)
            if info.filename==item['member']:
                d=bytearray(data);p=item['offset'];n=item['size']
                d[p:p+n]=NEW_MS.to_bytes(n,'little',signed=True)
                # Keep the existing log truthful without resizing a DEX string.
                old=('acquiring screen-off wakelock for '+str(original_ms)+'ms').encode()
                replacement=str(NEW_MS).rjust(len(str(original_ms)),'0')
                new=('acquiring screen-off wakelock for '+replacement+'ms').encode()
                if len(old)==len(new) and d.count(old)==1:
                    d=d.replace(old,new)
                d[12:32]=hashlib.sha1(d[32:]).digest()
                struct.pack_into('<I',d,8,zlib.adler32(d[12:])&0xffffffff)
                data=bytes(d)
            b.writestr(info,data)
    after=inspect(output)
    if after['timeout_ms']!=NEW_MS: raise ValueError('post-write decode mismatch')
    with zipfile.ZipFile(source) as a,zipfile.ZipFile(output) as b:
        changed=[n for n in a.namelist() if a.read(n)!=b.read(n)]
        if changed!=[item['member']]: raise ValueError('unrelated entry delta')
    return dict(changed_member=item['member'],literal_site=CLASS+'::onReceive -> WakeLock.acquire(J)',
                baseline_timeout_ms=original_ms,modified_timeout_ms=NEW_MS,scope='only AC screen-off grace; app wakelocks, battery state, credentials untouched',
                original_sha256=original_sha,modified_sha256=hashlib.sha256(output.read_bytes()).hexdigest(),dex_offset=item['offset'])

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('source',type=Path);p.add_argument('output',type=Path,nargs='?')
    a=p.parse_args()
    print(json.dumps(patch(a.source,a.output) if a.output else inspect(a.source),indent=2))
