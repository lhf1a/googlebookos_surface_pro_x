#!/usr/bin/env python3
"""Build a private Vulkan-only boot candidate; never writes a disk or deploys.

The fixed deployed baseline remains the opt-out path. Check all concatenated
cpio members in last-entry-wins order, retaining the local SSH addon last.
"""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import zlib

BASE_HASH='fa64305d7400ea3c7d262e3649c90dc4627c74dc7fd6cb563d52adda9d505275'
EXPECTED={
 'libadreno_utils.so':'6ef886f4b2d0d648e9ed3f6ee803ac70a2375421f90004bc6800fe8e229244ba',
 'libgsl.so':'b302ae0d5a6041be816ebe07c0c8c2686baf231b54182ee319731512db35523d',
 'libq2v77glnext.so':'17a9cb625c816bf9560489b53e15f20ad356db04ee0f1e83e5fbac03cb0fe3f3',
 'libq2v77qgl.so':'100a97a7c9cfa4b6d554cc1a8960a920de963aeba3b6c3a6bc2ea8d986472652',
 'libqti-mapper3-bridge.so':'dc0f8d0530154501fa849408d115fb22c14732b4e18a38977a01c791bbb3a0ac',
 'libquest2-ion-dmaheap.so':'56ce5b61ba12b966902abcddd51530ca12fa18b60f5e34bc6ef582e79eb631b8',
 'libquest2-v77-ahb.so':'c1c8f883d9de61392c9d1784028622f01692e37cc215e1114abdc3727516e7fd',
 'libquest2-vulkan-wrapper.so':'4dee0a10a128833d1b18909254c440dcda218ab529bd4b6dc110f637467cfae9',
 'vulkan-v77-bridge.so':'c6253f16ae790a5f845c563e2705e36a005aecf327ed312f7de4ddcbd0e60dce',
 'qgl_config.txt':'1f93a7a1446b2e934f12f23aa55514ee41852bf6df7851788d053cbd90e1f008',
}

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('base',type=Path);p.add_argument('bundle',type=Path);p.add_argument('output',type=Path)
 p.add_argument('--preserve-lab-ssh',action='store_true')
 a=p.parse_args()
 if not a.preserve_lab_ssh:raise ValueError('This lab baseline requires --preserve-lab-ssh')
 if a.output.exists():raise ValueError('Refusing existing output')
 root=Path(__file__).resolve().parents[1]
 spec=importlib.util.spec_from_file_location('cpio',root/'scripts/pack-sq2-vulkan.py')
 cpio=importlib.util.module_from_spec(spec);spec.loader.exec_module(cpio)
 sha=lambda b:hashlib.sha256(b).hexdigest()
 base=a.base.read_bytes()
 if sha(base)!=BASE_HASH:raise ValueError('Deployed baseline hash mismatch')
 before=cpio.entries(gzip.decompress(base))
 mode,init=before['init']
 anchor=b'apply-device-info > /logs/device-info.txt 2>&1 || fail device-info\n'
 if init.count(anchor)!=1 or b'closed-vulkan-start' in init:raise ValueError('Unexpected init hook')
 prefix='etc/sq2-vulkan-closed/'
 if any(n.startswith(prefix) for n in before):raise ValueError('Candidate already present')
 blobs={}
 for name,digest in EXPECTED.items():
  path=a.bundle/name
  if path.is_symlink():raise ValueError('Symlink input: '+name)
  data=path.read_bytes()
  if sha(data)!=digest:raise ValueError('Bundle mismatch: '+name)
  if name.endswith('.so') and (data[:6]!=b'\x7fELF\x02\x01' or int.from_bytes(data[18:20],'little')!=183):
   raise ValueError('Not arm64 ELF: '+name)
  blobs[name]=data
 if {p.name for p in a.bundle.glob('*.so')}!={n for n in EXPECTED if n.endswith('.so')}:
  raise ValueError('Unexpected bundle library')
 blobs['vulkan-version.xml']=b'<permissions><feature name="android.hardware.vulkan.version" version="4198400"/></permissions>\n'
 blobs['sq2-closed-vulkan.rc']=(root/'configs/sq2-closed-vulkan.rc').read_bytes().replace(b'\r\n',b'\n')
 blobs['SHA256SUMS']=''.join(f'{sha(data)}  {name}\n' for name,data in sorted(blobs.items())).encode()
 helper=(root/'probe/test2/closed-vulkan-start').read_bytes().replace(b'\r\n',b'\n')
 hook=b'closed-vulkan-start > /logs/closed-vulkan.txt 2>&1 || fail closed-vulkan-overlay\n'
 delta={'etc/sq2-vulkan-closed':(0o40755,b''),
        **{prefix+n:(0o100644,b) for n,b in blobs.items()},
        'bin/closed-vulkan-start':(0o100755,helper),
        'init':(mode,init.replace(anchor,hook+anchor))}
 for n in delta:
  parent=Path(n).parent.as_posix()
  if parent!='.' and (before.get(parent,delta.get(parent,(0,None)))[0]&0o170000)!=0o040000:
   raise ValueError('Missing directory: '+parent)
 rest=base;credential=None
 while rest:
  dec=zlib.decompressobj(31);payload=dec.decompress(rest)
  if not dec.eof:raise ValueError('Incomplete gzip member')
  files=cpio.entries(payload)
  if 'etc/shadow' in files and 'bin/dropbear' in files:
   credential=rest[:len(rest)-len(dec.unused_data)]
  rest=dec.unused_data
 if not credential:raise ValueError('Missing complete lab SSH addon')
 archive=b''.join(cpio.entry(n,b,m) for n,(m,b) in delta.items())+cpio.entry('TRAILER!!!',b'',0)
 archive+=b'\0'*(-len(archive)%512)
 output=base+gzip.compress(archive,mtime=0)+credential
 after=cpio.entries(gzip.decompress(output))
 changed={n for n in before.keys()|after.keys() if before.get(n)!=after.get(n)}
 if changed!=set(delta):raise ValueError('Unexpected final-entry delta')
 # The entire original vendor overlay, including Mesa, feature XML, audio and
 # policy remains identical in the archive. Only the boot helper selects Vulkan.
 for n in before:
  if n!='init' and before[n]!=after.get(n):raise ValueError('Baseline changed: '+n)
 a.output.parent.mkdir(parents=True,exist_ok=True)
 with a.output.open('xb') as f:f.write(output)
 report={'base_sha256':sha(base),'output_sha256':sha(output),'changed':sorted(changed),
         'bundle_sha256':EXPECTED,'password_addon_last':True,'mesa_archive_unchanged':True,
         'opt_out':'sq2.vulkan=mesa','reboot_validated':False,'cts_passed':False}
 a.output.with_suffix('.audit.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps(report,indent=2))

if __name__=='__main__':main()
