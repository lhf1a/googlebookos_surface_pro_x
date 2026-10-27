#!/usr/bin/env python3
"""Add only the CPU frequency boot hook to the audited live initramfs."""
import argparse,gzip,hashlib,importlib.util,json,zlib
from pathlib import Path

spec=importlib.util.spec_from_file_location('cpio',Path(__file__).with_name('pack-sq2-vulkan.py'))
cpio=importlib.util.module_from_spec(spec);spec.loader.exec_module(cpio)
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('base',type=Path);p.add_argument('helper',type=Path);p.add_argument('output',type=Path)
a=p.parse_args();assert not a.output.exists()
sha=lambda b:hashlib.sha256(b).hexdigest()
base=a.base.read_bytes()
assert sha(base)=='f8fb83358edeead8ac10c53e20b53b06b9e1597ab8268ab345a262a744691f50'
before=cpio.entries(gzip.decompress(base));mode,init=before['init']
assert 'lib/modules/6.3.8-sq2-android1/kernel/drivers/cpufreq/qcom-cpufreq-hw.ko' in before
anchor=b'    for m in hid_multitouch '
assert init.count(anchor)==1 and b'SQ2_TSENS_V1' in init and b'SQ2_CPUFREQ_V1' not in init
hook=b'''    # SQ2_CPUFREQ_V1: matching hardware driver before Android starts.
    cpu-frequency-start > /logs/cpu-frequency.txt 2>&1 || echo 'SQ2_CPUFREQ_NOT_READY: see RAM log'
'''
helper=a.helper.read_bytes().replace(b'\r\n',b'\n')
delta={'init':(mode,init.replace(anchor,hook+anchor)),'bin/cpu-frequency-start':(0o100755,helper)}
assert 'bin' in before
rest=base;credential=None
while rest:
 dec=zlib.decompressobj(31);files=cpio.entries(dec.decompress(rest));assert dec.eof
 if 'etc/shadow' in files and 'bin/dropbear' in files:credential=rest[:len(rest)-len(dec.unused_data)]
 rest=dec.unused_data
assert credential
archive=b''.join(cpio.entry(n,body,mode) for n,(mode,body) in delta.items())+cpio.entry('TRAILER!!!',b'',0)
archive+=b'\0'*(-len(archive)%512)
output=base+gzip.compress(archive,mtime=0)+credential
after=cpio.entries(gzip.decompress(output))
assert {n for n in before.keys()|after.keys() if before.get(n)!=after.get(n)}==set(delta)
with a.output.open('xb') as f:f.write(output)
audit={'base_sha256':sha(base),'output_sha256':sha(output),'changed':list(delta),'password_addon_last':True}
a.output.with_suffix('.audit.json').write_text(json.dumps(audit,indent=2)+'\n');print(json.dumps(audit,indent=2))
