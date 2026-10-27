#!/usr/bin/env python3
"""Append matching TSENS module and bounded early boot hook; retain private SSH last."""
import argparse,gzip,hashlib,importlib.util,json,zlib
from pathlib import Path

spec=importlib.util.spec_from_file_location('cpio',Path(__file__).with_name('pack-sq2-vulkan.py'))
cpio=importlib.util.module_from_spec(spec);spec.loader.exec_module(cpio)
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('base',type=Path);p.add_argument('module',type=Path);p.add_argument('output',type=Path)
p.add_argument('--expected-base-sha256',required=True)
a=p.parse_args()
assert not a.output.exists()
base=a.base.read_bytes();sha=lambda b:hashlib.sha256(b).hexdigest()
assert sha(base)==a.expected_base_sha256
before=cpio.entries(gzip.decompress(base))
mode,init=before['init']
head=b'if [ "$smoke" = 0 ] && [ "$ssd_smoke" = 0 ]; then\n'
anchor=head+b'    for m in hid_multitouch '
assert init.count(anchor)==1
module=a.module.read_bytes()
assert b'vermagic=6.3.8-sq2-android1 SMP mod_unload aarch64\0' in module
path='lib/modules/6.3.8-sq2-android1/kernel/drivers/thermal/qcom'
hook=b'''    # SQ2_TSENS_V1: matching module before Android's incompatible donor loader.
    if ! insmod /lib/modules/6.3.8-sq2-android1/kernel/drivers/thermal/qcom/qcom_tsens.ko >> /logs/module-load.txt 2>&1; then
        # The high-frequency experiment must not boot without its sensor driver.
        if [ -d /sys/firmware/devicetree/base/soc@0/gpu@2c00000/opp-table/opp-661000000 ]; then
            fail gpu-tsens-module
        fi
    fi
'''
assert path+'/qcom_tsens.ko' not in before
delta={} if path in before else {path:(0o40755,b'')}
delta.update({path+'/qcom_tsens.ko':(0o100644,module),'init':(mode,init.replace(anchor,head+hook+b'    for m in hid_multitouch '))})
assert path.rsplit('/',1)[0] in before
rest=base;credential=None
while rest:
 dec=zlib.decompressobj(31);files=cpio.entries(dec.decompress(rest));assert dec.eof
 if 'etc/shadow' in files and 'bin/dropbear' in files:credential=rest[:len(rest)-len(dec.unused_data)]
 rest=dec.unused_data
assert credential, 'This lab packer requires the local SSH addon'
archive=b''.join(cpio.entry(n,body,mode) for n,(mode,body) in delta.items())+cpio.entry('TRAILER!!!',b'',0)
archive+=b'\0'*(-len(archive)%512)
output=base+gzip.compress(archive,mtime=0)+credential
after=cpio.entries(gzip.decompress(output))
assert {n for n in before.keys()|after.keys() if before.get(n)!=after.get(n)}==set(delta)
with a.output.open('xb') as f:f.write(output)
audit={'base_sha256':sha(base),'output_sha256':sha(output),'module_sha256':sha(module),'changed':list(delta),'password_addon_last':True}
a.output.with_suffix('.audit.json').write_text(json.dumps(audit,indent=2)+'\n')
print(json.dumps(audit,indent=2))
