#!/usr/bin/env python3
"""Inspect all concatenated gzip/newc members, applying last-entry-wins order."""
import gzip
import hashlib
import json
from pathlib import Path
import sys

path=Path(sys.argv[1])
data=gzip.decompress(path.read_bytes())
pos=0
archive=0
found={}
while pos < len(data):
    if data[pos:pos+6] not in (b'070701', b'070702'):
        assert data[pos] == 0, f'Invalid header at {pos}'
        pos+=1
        continue
    fields=[int(data[pos+6+i*8:pos+14+i*8],16) for i in range(13)]
    size,namesize=fields[6],fields[11]
    name=data[pos+110:pos+110+namesize-1].decode()
    begin=(pos+110+namesize+3)&~3
    body=data[begin:begin+size]
    assert len(body)==size
    pos=(begin+size+3)&~3
    if name=='TRAILER!!!':
        archive+=1
        continue
    name=name.lstrip('./')
    if name in ('bin/iptsd','bin/touch-services','bin/refresh-iptsd-input-nodes','init') or 'iptsd' in name and size:
        item=dict(member=archive,bytes=size,sha256=hashlib.sha256(body).hexdigest())
        if size < 12000 and name!='bin/iptsd': item['text']=body.decode(errors='replace')
        found.setdefault(name,[]).append(item)
out=dict(image=str(path),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),cpio_members=archive,files=found)
if len(sys.argv)>2: Path(sys.argv[2]).write_text(json.dumps(out,indent=2)+'\n')
for name, entries in found.items():
    if name in ('bin/iptsd','etc/iptsd.conf') or name.endswith('.conf'):
        print(name, json.dumps(entries))
print('cpio_members=',archive)
