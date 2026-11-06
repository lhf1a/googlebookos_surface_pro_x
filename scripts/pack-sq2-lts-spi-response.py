#!/usr/bin/env python3
"""Replace SPI-HID in a paired private 6.18 initramfs, retaining all other entries."""
import argparse,gzip,hashlib,importlib.util,json,posixpath,subprocess,tempfile
from pathlib import Path,PurePosixPath
ROOT=Path(__file__).resolve().parents[1]
s=importlib.util.spec_from_file_location('pack',ROOT/'scripts/pack-sq2-lts-audio-ssam.py');p=importlib.util.module_from_spec(s);s.loader.exec_module(p)
sha=lambda b:hashlib.sha256(b).hexdigest()
def run(args):
 q=subprocess.run([str(a) for a in args],capture_output=True,text=True)
 print(json.dumps(dict(command=[str(a) for a in args],stdout=q.stdout,stderr=q.stderr,exit=q.returncode)),flush=True)
 assert q.returncode==0
 return q.stdout.strip()
def main():
 ap=argparse.ArgumentParser(description=__doc__)
 for n in ('base','module','system_map','output'):ap.add_argument(n,type=Path)
 ap.add_argument('--expected-base-sha256',required=True)
 ap.add_argument('--preserve-lab-ssh',action='store_true')
 a=ap.parse_args();assert not a.output.exists()
 base=a.base.read_bytes();assert sha(base)==a.expected_base_sha256
 before=p.cpio.entries(gzip.decompress(base));tail=p.credential_tail(base)
 assert bool(tail)==a.preserve_lab_ssh
 module=a.module.read_bytes();assert module[:6]==b'\x7fELF\x02\x01' and int.from_bytes(module[18:20],'little')==183
 vm=run(['modinfo','-F','vermagic',a.module]);release=vm.split()[0]
 assert release.startswith('6.18.') and '/' not in release
 assert run(['modinfo','-F','name',a.module])=='spi_hid'
 prefix='lib/modules/'+release+'/'
 paths=[n for n in before if n.endswith('/spi-hid.ko')];assert len(paths)==1 and paths[0].startswith(prefix)
 changes={paths[0]:(before[paths[0]][0],module)}
 with tempfile.TemporaryDirectory(prefix='sq2-touch-index-') as td:
  stage=Path(td)
  for n,(mode,body) in before.items():
   if n.startswith(prefix) and mode&0o170000==0o100000:
    assert '..' not in PurePosixPath(n).parts
    f=stage/n;f.parent.mkdir(parents=True,exist_ok=True);f.write_bytes(body)
  assert run(['modinfo','-F','vermagic',stage/paths[0]])==vm
  assert run(['modinfo','-F','alias',stage/paths[0]])==run(['modinfo','-F','alias',a.module])
  assert run(['modinfo','-F','depends',stage/paths[0]])==run(['modinfo','-F','depends',a.module])
  (stage/paths[0]).write_bytes(module)
  run(['depmod','-b',stage,'-e','-F',a.system_map,release])
  for f in (stage/prefix).glob('modules.*'):
   n=f.relative_to(stage).as_posix();body=f.read_bytes()
   if n not in before or before[n][1]!=body:changes[n]=(before.get(n,(0o100644,b''))[0],body)
 for n,(mode,body) in before.items():
  if not (n.endswith('SHA256SUMS') or n.endswith('.sha256')):continue
  changed=False;lines=[]
  for line in body.decode().splitlines():
   pair=line.split(None,1)
   if len(pair)==2:
    target=posixpath.normpath(posixpath.join(posixpath.dirname(n),pair[1].lstrip('*'))).lstrip('/')
    if target in changes:line=sha(changes[target][1])+'  '+pair[1].lstrip('*');changed=True
   lines.append(line)
  if changed:changes[n]=(mode,('\n'.join(lines)+'\n').encode())
 raw=b''.join(p.cpio.entry(n,body,mode) for n,(mode,body) in changes.items())+p.cpio.entry('TRAILER!!!',b'',0)
 raw+=b'\0'*(-len(raw)%512)
 result=base+gzip.compress(raw,mtime=0)+tail
 after=p.cpio.entries(gzip.decompress(result));assert after==before|changes
 assert all(before[n]==after[n] for n in before if n not in changes)
 a.output.parent.mkdir(parents=True,exist_ok=True)
 with a.output.open('xb') as f:f.write(result)
 assert a.output.read_bytes()==result and a.base.read_bytes()==base
 report=dict(baseline_sha256=sha(base),sha256=sha(result),bytes=len(result),changed_entries=sorted(changes),module_sha256=sha(module),kernel_release=release,private_package=True,credentials_unchanged=True,deployed=False,reboot_verified=False)
 a.output.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
