#!/usr/bin/env python3
"""Append only verified SQ2 thermal config and read-only battery compatibility."""
import argparse,gzip,hashlib,importlib.util,json,zlib
from pathlib import Path
spec=importlib.util.spec_from_file_location('cpio',Path(__file__).with_name('pack-sq2-vulkan.py'))
cpio=importlib.util.module_from_spec(spec);spec.loader.exec_module(cpio)
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('base',type=Path);p.add_argument('output',type=Path)
a=p.parse_args();assert not a.output.exists()
root=Path(__file__).resolve().parent.parent
sha=lambda b:hashlib.sha256(b).hexdigest()
base=a.base.read_bytes();assert sha(base)=='e16a7a0100ccb4b3893d5e64a88ca32a712e4871e36f7d47a9f4b07d2cad677a'
before=cpio.entries(gzip.decompress(base));mode,init=before['init']
anchor=b'apply-device-info > /logs/device-info.txt 2>&1 || fail device-info\n'
assert init.count(anchor)==1 and b'resource-monitor-start' not in init
hook=b"resource-monitor-start > /logs/resource-monitor.txt 2>&1 || echo 'SQ2_RESOURCE_MONITOR_NOT_READY: see RAM log'\n"
module=(root/'out/resource-monitor/sq2_battery_compat.ko').read_bytes()
assert b'vermagic=6.3.8-sq2-android1 SMP mod_unload aarch64\0' in module
delta={'etc/sq2-monitor':(0o40755,b''),
 'etc/sq2-monitor/thermal-info.json':(0o100644,(root/'configs/sq2-thermal-info.json').read_bytes()),
 'etc/sq2-monitor/sq2_battery_compat.ko':(0o100644,module),
 'bin/resource-monitor-start':(0o100755,(root/'probe/test2/resource-monitor-start').read_bytes().replace(b'\r\n',b'\n')),
 'init':(mode,init.replace(anchor,anchor+hook))}
rest=base;credential=None
while rest:
 dec=zlib.decompressobj(31);files=cpio.entries(dec.decompress(rest));assert dec.eof
 if 'etc/shadow' in files and 'bin/dropbear' in files:credential=rest[:len(rest)-len(dec.unused_data)]
 rest=dec.unused_data
assert credential and 'etc' in before and 'bin' in before
archive=b''.join(cpio.entry(n,body,mode) for n,(mode,body) in delta.items())+cpio.entry('TRAILER!!!',b'',0)
archive+=b'\0'*(-len(archive)%512)
output=base+gzip.compress(archive,mtime=0)+credential
after=cpio.entries(gzip.decompress(output))
assert {n for n in before.keys()|after.keys() if before.get(n)!=after.get(n)}==set(delta)
with a.output.open('xb') as f:f.write(output)
audit={'base_sha256':sha(base),'output_sha256':sha(output),'module_sha256':sha(module),'changed':list(delta),'password_addon_last':True}
a.output.with_suffix('.audit.json').write_text(json.dumps(audit,indent=2)+'\n');print(json.dumps(audit,indent=2))
