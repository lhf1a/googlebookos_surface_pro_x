"""Strict touch-only runtime overlay; never deploy or collect device input."""
from pathlib import Path
import gzip,hashlib,json,zlib

PATHS={'bin/iptsd','bin/touch-calibration-boot','bin/touch-calibration-profile',
       'bin/touch-noise-profile','etc/sq2-touch-calibration/wizard.apk'}

def integrate(raw,root,bundle,c):
    root,bundle=Path(root),Path(bundle)
    manifest=json.loads((bundle/'touch-bundle.json').read_text())
    sha=lambda b:hashlib.sha256(b).hexdigest()
    if set(manifest['files'])!=PATHS:raise ValueError('Incomplete touch bundle')
    for name,h in manifest['source_sha256'].items():
        if sha((root/name).read_bytes().replace(b'\r\n',b'\n'))!=h:raise ValueError('Stale touch source: '+name)
    before=c.entries(gzip.decompress(raw));e=dict(before)
    for name,h in manifest['files'].items():
        b=(bundle/'payload'/name).read_bytes()
        if sha(b)!=h:raise ValueError('Touch payload hash: '+name)
        e[name]=(0o100644 if name.endswith('.apk') else 0o100755,b)
    # Preserve the same credential member last, with no credential edits.
    rest=raw;last=None
    while rest:
        d=zlib.decompressobj(31);member_entries=c.entries(d.decompress(rest))
        if not d.eof:raise ValueError('gzip member')
        member=rest[:len(rest)-len(d.unused_data)]
        if 'etc/shadow' in member_entries and 'bin/dropbear' in member_entries:last=member
        rest=d.unused_data
    if last is None:raise ValueError('Credential member absent')
    body=b''.join(c.entry(n,b,m) for n,(m,b) in sorted(e.items(),key=lambda kv:(kv[0].count('/'),kv[0])))+c.entry('TRAILER!!!',b'',0)
    body+=b'\0'*(-len(body)%512)
    packed=gzip.compress(body,compresslevel=3,mtime=0)+last
    if c.entries(gzip.decompress(packed))!=e:raise ValueError('Touch last-entry audit')
    changed=sorted(n for n in e if e[n]!=before.get(n))
    if not set(changed)<=PATHS:raise ValueError('Non-touch payload changed')
    return packed,{'changed':changed,'manifest_sha256':sha((bundle/'touch-bundle.json').read_bytes()),
                   'unrelated_entries_byte_identical':True,'credential_member_last':True}
