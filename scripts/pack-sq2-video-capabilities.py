#!/usr/bin/env python3
"""Build a private capability-parser boot candidate; does not deploy or boot.

Only native core/decoder and their checksum manifest change. Existing C2 boot
service is retained: updated Android owner/mapper still require lab staging.
"""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import zlib

BASE = 'a503842819c41206b6018cf34cd00e7b17b138f56e7c5b78db42c5ab9a5e0e9a'
MODULES = {
    'venus-core.ko': '394000635ed3345c52e6bb60f18da3320fc59da5a3efbea47d0f54b876cce2e1',
    'venus-dec.ko': 'b11749cc6b6a1f9a7961d582625df0342dc0cad4af3bd2e33075fc912a8d917c',
}
MODULES_V3 = {
    'venus-core.ko': '50afdd9164eb9eb4c2f7882871045dcdd52c88ef0f32e219ab875c6251ea644b',
    'venus-dec.ko': '257ffcd19294a3a16ddb667a20ed3ae0196471b206de947c4ed8e1f0fb585d92',
}
def sha(data):
    return hashlib.sha256(data).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('base',type=Path)
    p.add_argument('modules',type=Path)
    p.add_argument('output',type=Path)
    p.add_argument('--revision', choices=('1','3'), default='1')
    a=p.parse_args()
    modules = MODULES_V3 if a.revision == '3' else MODULES
    if a.output.exists():raise ValueError('output exists')
    spec=importlib.util.spec_from_file_location('cpio',Path(__file__).with_name('pack-sq2-vulkan.py'))
    c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
    base=a.base.read_bytes()
    if sha(base)!=BASE:raise ValueError('unexpected base')
    before=c.entries(gzip.decompress(base));delta={};prefix='etc/sq2-video-native/'
    for name,digest in modules.items():
        blob=(a.modules/name).read_bytes()
        if sha(blob)!=digest:raise ValueError('unexpected candidate: '+name)
        delta[prefix+name]=(before[prefix+name][0],blob)
    manifest_name=prefix+'SHA256SUMS'
    rows=[];seen=set()
    for line in before[manifest_name][1].decode().splitlines():
        digest,name=line.split()
        if name in seen or Path(name).name!=name:raise ValueError('bad manifest')
        seen.add(name)
        if sha(before[prefix+name][1])!=digest:raise ValueError('bad base manifest')
        rows.append(f'{modules.get(name,digest)}  {name}\n')
    if not set(modules)<=seen:raise ValueError('modules absent from manifest')
    delta[manifest_name]=(before[manifest_name][0],''.join(rows).encode())
    rest=base;credentials=None
    while rest:
        dec=zlib.decompressobj(31);payload=dec.decompress(rest)
        if not dec.eof:raise ValueError('truncated gzip')
        entries=c.entries(payload)
        if 'etc/shadow' in entries and 'bin/dropbear' in entries:
            credentials=rest[:len(rest)-len(dec.unused_data)]
        rest=dec.unused_data
    if credentials is None:raise ValueError('missing private SSH addon')
    archive=b''.join(c.entry(n,data,mode) for n,(mode,data) in delta.items())
    archive+=c.entry('TRAILER!!!',b'',0);archive+=b'\0'*(-len(archive)%512)
    output=base+gzip.compress(archive,mtime=0)+credentials
    after=c.entries(gzip.decompress(output))
    changed={n for n in before.keys()|after.keys() if before.get(n)!=after.get(n)}
    if changed!=set(delta):raise ValueError('unexpected archive changes')
    if any(after[n]!=v for n,v in delta.items()):raise ValueError('delta not effective')
    with a.output.open('xb') as f:f.write(output)
    report=dict(revision=a.revision,base_sha256=BASE,output_sha256=sha(output),changed=sorted(changed),
                credentials_last=True,boot_tested=False,android_owner_unchanged=True)
    a.output.with_suffix('.audit.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()
