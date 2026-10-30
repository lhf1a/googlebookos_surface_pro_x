#!/usr/bin/env python3
"""Small, audited addon to the already boot-tested runtime-v1 image; no disk IO."""
import argparse,gzip,hashlib,importlib.util,json,zlib
from pathlib import Path
R=Path(__file__).resolve().parents[1]
BASE='8b6c1f9a83aee85cf86aca4342147ee50bd43fb37e17d8f3a9750a759761484b'
XML='5bfe57faaae9fc5a462ba48300b5f967bc7170fde609e9d6ed55f73c206d544b'
sha=lambda b:hashlib.sha256(b).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('base',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
 if a.output.exists():raise ValueError('output exists')
 base=a.base.read_bytes()
 if sha(base)!=BASE:raise ValueError('unverified runtime-v1 image')
 s=importlib.util.spec_from_file_location('cpio',R/'scripts/pack-sq2-vulkan.py');c=importlib.util.module_from_spec(s);s.loader.exec_module(c)
 before=c.entries(gzip.decompress(base));delta={}
 xml=(R/'configs/sq2-media-codecs-c2.xml').read_bytes().replace(b'\r\n',b'\n')
 if sha(xml)!=XML:raise ValueError('unverified codec XML')
 prefix='etc/sq2-c2-runtime-boot/'
 delta[prefix+'media_codecs_c2.xml']=(0o100644,xml)
 sums=before[prefix+'SHA256SUMS'][1].decode().splitlines();new=[]
 for line in sums:
  digest,name=line.split()
  if name=='media_codecs_c2.xml':digest=sha(xml)
  else:assert sha(before[prefix+name][1])==digest
  new.append(digest+'  '+name+'\n')
 delta[prefix+'SHA256SUMS']=(0o100644,''.join(new).encode())
 for dest,src in [('video-codec-manifest-stage','video-runtime-stage'),('video-runtime-framework-boot','video-runtime-framework-boot'),('video-decoder-lab-start','video-decoder-lab-start')]:
  delta['bin/'+dest]=(0o100755,(R/'probe/test2'/src).read_bytes().replace(b'\r\n',b'\n'))
 rest=base;credentials=None
 while rest:
  d=zlib.decompressobj(31);files=c.entries(d.decompress(rest));assert d.eof
  if 'etc/shadow' in files and 'bin/dropbear' in files:credentials=rest[:len(rest)-len(d.unused_data)]
  rest=d.unused_data
 if credentials is None:raise ValueError('private SSH final member missing')
 raw=b''.join(c.entry(n,b,m) for n,(m,b) in delta.items())+c.entry('TRAILER!!!',b'',0)
 raw+=b'\0'*(-len(raw)%512)
 addon=gzip.compress(raw,mtime=0)+credentials
 after=c.entries(gzip.decompress(base+addon))
 changed={n for n in before.keys()|after.keys() if before.get(n)!=after.get(n)}
 assert changed==set(delta)
 assert all(after[n]==v for n,v in before.items() if n not in delta)
 a.output.write_bytes(addon)
 report=dict(base_sha256=BASE,addon_sha256=sha(addon),bytes=len(addon),changed=sorted(changed),credentials_last=True,hardware_tested=False)
 a.output.with_suffix('.audit.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
