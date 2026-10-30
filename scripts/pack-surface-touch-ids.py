#!/usr/bin/env python3
"""Append only the verified Surface touch device-ID helper changes.

Private lab archive; preserve credentials last; never writes a device.
"""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import zlib

R=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('cpio', R/'scripts/pack-sq2-vulkan.py')
c=importlib.util.module_from_spec(spec); spec.loader.exec_module(c)
sha=lambda b: hashlib.sha256(b).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('base',type=Path); p.add_argument('output',type=Path)
    p.add_argument('--expected-base-sha256',required=True)
    a=p.parse_args()
    if a.output.exists(): raise ValueError('Refusing overwrite')
    base=a.base.read_bytes()
    if sha(base)!=a.expected_base_sha256: raise ValueError('Baseline hash mismatch')
    before=c.entries(gzip.decompress(base)); delta={}
    for name in ('touch-services','refresh-iptsd-input-nodes'):
        n='bin/'+name
        assert n in before
        body=(R/'probe/test2'/name).read_bytes().replace(b'\r\n',b'\n')
        subprocess.run(['sh','-n'],input=body,check=True)
        delta[n]=(before[n][0],body)
    rest=base; credentials=None
    while rest:
        dec=zlib.decompressobj(31); files=c.entries(dec.decompress(rest)); assert dec.eof
        if 'etc/shadow' in files and 'bin/dropbear' in files:
            credentials=rest[:len(rest)-len(dec.unused_data)]
        rest=dec.unused_data
    assert credentials
    archive=b''.join(c.entry(n,b,m) for n,(m,b) in delta.items())+c.entry('TRAILER!!!',b'',0)
    archive+=b'\0'*(-len(archive)%512)
    output=base+gzip.compress(archive,mtime=0)+credentials
    after=c.entries(gzip.decompress(output))
    changed={n for n in before.keys()|after.keys() if before.get(n)!=after.get(n)}
    assert changed==set(delta)
    assert all(after[n]==v for n,v in before.items() if n not in delta)
    assert sha(after['bin/iptsd'][1])=='5c0fd552b4bf00d12e89daff1570c83a7e31525e0971379fc0b1b97399613fbd'
    with a.output.open('xb') as f: f.write(output)
    report=dict(base_sha256=sha(base),output_sha256=sha(output),changed=sorted(changed),
                password_addon_last=True,ghost_touch_fix=False,reboot_verified=False)
    a.output.with_suffix('.audit.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))

if __name__=='__main__': main()
