#!/usr/bin/env python3
"""Microphone-only adaptation of the verified resource-monitor initramfs.

Includes the stock-transport HAL with only its mic address overwrite removed.
Never imports the old experimental playback patch. Credentials remain last.
"""
import argparse, gzip, hashlib, importlib.util, json, zlib
from pathlib import Path
spec=importlib.util.spec_from_file_location('cpio',Path(__file__).with_name('pack-sq2-vulkan.py'))
cpio=importlib.util.module_from_spec(spec);spec.loader.exec_module(cpio)
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('base',type=Path);p.add_argument('hal',type=Path);p.add_argument('output',type=Path)
a=p.parse_args();assert not a.output.exists()
root=Path(__file__).resolve().parent.parent
sha=lambda b:hashlib.sha256(b).hexdigest()
base=a.base.read_bytes();assert sha(base)=='d519b958be0227005a97081efc75603325823bfcb526891f6532cee31bfb3936'
hal=a.hal.read_bytes();assert sha(hal)=='774cd7e731bd5a5fcda6edbde6e97464bd0e04acc1603c3be7b0dbe7a69561ea'
before=cpio.entries(gzip.decompress(base))
def read(name): return (root/name).read_bytes().replace(b'\r\n',b'\n')
helper=read('probe/test2/audio-framework-boot')
assert b'audio-microphone-route enable\n' in helper
delta={
 'etc/sq2-audio/audio-hal':(0o100755,hal),
 'etc/sq2-audio/primary_audio_policy_configuration.xml':(0o100644,read('configs/primary_audio_policy_sq2.xml')),
 'bin/audio-framework-boot':(0o100755,helper),
 'bin/audio-microphone-route':(0o100755,read('probe/test2/audio-microphone-route')),
}
for n in delta:
 assert str(Path(n).parent) in before
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
for n in ['init','bin/iptsd','bin/audio-speaker-route','bin/resource-monitor-start','etc/shadow','bin/dropbear']:
 assert before[n]==after[n],n
with a.output.open('xb') as f:f.write(output)
audit=dict(base_sha256=sha(base),output_sha256=sha(output),hal_sha256=sha(hal),changed=list(delta),password_addon_last=True,reboot_validated=False)
a.output.with_suffix('.audit.json').write_text(json.dumps(audit,indent=2)+'\n');print(json.dumps(audit,indent=2))
