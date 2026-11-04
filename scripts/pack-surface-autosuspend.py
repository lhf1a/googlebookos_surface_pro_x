#!/usr/bin/env python3
"""Isolated native autosuspend payload; preserve current runtime and final SSH layer."""
import argparse,gzip,hashlib,importlib.util,json,zlib,subprocess
from pathlib import Path
R=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('baseline',type=Path);p.add_argument('framework_jar',type=Path)
p.add_argument('lab_helper',type=Path);p.add_argument('output',type=Path)
p.add_argument('--framework-baseline',type=Path,required=True)
p.add_argument('--system-suspend',type=Path,required=True)
p.add_argument('--expected-baseline-sha256')
a=p.parse_args()
sha=lambda b:hashlib.sha256(b).hexdigest()
raw=a.baseline.read_bytes()
if a.expected_baseline_sha256:
 assert sha(raw)==a.expected_baseline_sha256
assert a.system_suspend.stat().st_size>0
assert a.framework_baseline.stat().st_size>0 and a.framework_jar.stat().st_size>0
assert not a.output.exists()
spec=importlib.util.spec_from_file_location('cpio',R/'scripts/pack-sq2-vulkan.py');c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
before=c.entries(gzip.decompress(raw));init=before['init'][1]
anchor=b'mount --bind /newroot/debug_ramdisk/keystore2.rc /newroot/system/etc/init/keystore2.rc || fail keystore-debug-overlay\n'
assert init.count(anchor)==1
init=init.replace(anchor,anchor+b'framework-suspend-stage || fail framework-suspend-stage\n')
anchor=b'lab-keep-awake > /logs/lab-keep-awake.txt 2>&1 &\n'
assert init.count(anchor)==1
init=init.replace(anchor,anchor+b'framework-suspend-start > /logs/framework-suspend-start.txt 2>&1 &\n')
added={
 'init':(before['init'][0],init),
 'bin/lab-keep-awake':(before['bin/lab-keep-awake'][0],a.lab_helper.read_bytes()),
 'bin/framework-suspend-stage':(0o100755,(R/'probe/test2/framework-suspend-stage').read_bytes()),
 'bin/framework-suspend-start':(0o100755,(R/'probe/test2/framework-suspend-start').read_bytes()),
 'etc/surface-pm':(0o40755,b''),
 'etc/surface-pm/desktop-services.jar':(0o100644,a.framework_jar.read_bytes()),
 'etc/surface-pm/system-suspend.sha256':(0o100644,(sha(a.system_suspend.read_bytes())+'\n').encode()),
 'etc/surface-pm/framework.sha256':(0o100644,('baseline '+sha(a.framework_baseline.read_bytes())+'\ncandidate '+sha(a.framework_jar.read_bytes())+'\n').encode()),
}
for n,(m,b) in added.items():
 if n=='init' or n.startswith('bin/'):
  subprocess.run(['sh','-n'],input=b,check=True)
rest=raw;credential=None
while rest:
 d=zlib.decompressobj(31);members=c.entries(d.decompress(rest));assert d.eof
 member=rest[:len(rest)-len(d.unused_data)]
 if 'etc/shadow' in members and 'bin/dropbear' in members and 'init' not in members:credential=member
 rest=d.unused_data
cpio=b''.join(c.entry(n,b,m) for n,(m,b) in added.items())+c.entry('TRAILER!!!',b'',0)
cpio+=b'\0'*(-len(cpio)%512)
result=raw+gzip.compress(cpio,compresslevel=3,mtime=0)+(credential or b'')
after=c.entries(gzip.decompress(result));expected=dict(before);expected.update(added)
assert after==expected
assert credential is None or result.endswith(credential)
a.output.write_bytes(result)
assert c.entries(gzip.decompress(a.output.read_bytes()))==expected
summary=dict(output=str(a.output.resolve()),sha256=sha(result),bytes=len(result),
 changed=sorted(n for n in after if after[n]!=before.get(n)),unchanged_effective_entries=sum(after[n]==before[n] for n in before),
 credentials_final_and_unchanged=True, separate_credential_layer=credential is not None,baseline_sha256=sha(raw),
 scope='packaging only; kernel/real key/lockscreen tests separate')
a.output.with_suffix(a.output.suffix+'.json').write_text(json.dumps(summary,indent=2))
print(json.dumps(summary,indent=2))
