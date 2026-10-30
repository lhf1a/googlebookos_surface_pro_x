#!/usr/bin/env python3
"""Independent opt-in boot candidate. No disk writes or reboot."""
from pathlib import Path
import argparse, gzip, hashlib, importlib.util, io, json, tarfile, zlib
ROOT=Path(__file__).resolve().parents[1]
BASE='e519556b0d5e5aa7ccb98772ef58fceee6fad2647f905cf85a914cab58a60b7b'
RUNTIME='de2d825b34e09158076cf6b14f5777d0f843d6f5cc782d3961d7b2a6531b7f50'
CODEC_XML='5bfe57faaae9fc5a462ba48300b5f967bc7170fde609e9d6ed55f73c206d544b'
sha=lambda b:hashlib.sha256(b).hexdigest()

def boot_codec_xml():
    # The immutable v1 userdata runtime stays byte-identical. Android reads the
    # separately checksummed vendor XML staged before init, not the tar copy.
    xml=(ROOT/'configs/sq2-media-codecs-c2.xml').read_bytes().replace(b'\r\n',b'\n')
    if sha(xml)!=CODEC_XML:raise ValueError('unverified boot codec capabilities')
    return xml

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('base','runtime','output'):p.add_argument(n,type=Path)
    a=p.parse_args()
    if a.output.exists():raise ValueError('output exists')
    base=a.base.read_bytes(); runtime=a.runtime.read_bytes()
    if sha(base)!=BASE or sha(runtime)!=RUNTIME:raise ValueError('unverified input')
    spec=importlib.util.spec_from_file_location('cpio',ROOT/'scripts/pack-sq2-vulkan.py')
    c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
    before=c.entries(gzip.decompress(base));delta={}
    def put(name,data,mode=0o100644):
        parent=Path(name).parent.as_posix()
        if parent!='.' and parent not in before and parent not in delta:put(parent,b'',0o40755)
        delta[name]=(mode,data)
    with tarfile.open(fileobj=io.BytesIO(runtime)) as tar:
        sums=tar.extractfile('SHA256SUMS').read()
    xml=boot_codec_xml()
    files={'runtime.tar':runtime,'runtime.sha256':(RUNTIME+'  runtime.tar\n').encode(),
           'runtime-files.sha256':sums,'media_codecs_c2.xml':xml,
           'install.sh':(ROOT/'probe/video/sq2-c2-runtime-install.sh').read_bytes().replace(b'\r\n',b'\n'),
           'service.sh':b'#!/system/bin/sh\nset -eu\n/system/bin/sh /vendor/etc/sq2-codec2/install.sh\nexec /system/bin/sh /data/vendor/sq2-codec2/v1/start.sh "$@"\n',
           'service.rc':(ROOT/'configs/sq2-c2-experimental.rc').read_bytes().replace(b'\r\n',b'\n'),
           'manifest.xml':b'<manifest version="2.0" type="device"><hal format="aidl"><name>android.hardware.media.c2</name><version>2</version><fqname>IComponentStore/default1</fqname></hal></manifest>\n'}
    # hal class starts after post-fs-data; installer also checks the actual mount.
    for n,b in files.items():put('etc/sq2-c2-runtime-boot/'+n,b)
    put('etc/sq2-c2-runtime-boot/SHA256SUMS',''.join(sha(b)+'  '+n+'\n' for n,b in sorted(files.items())).encode())
    put('bin/video-codec-manifest-stage',(ROOT/'probe/test2/video-runtime-stage').read_bytes().replace(b'\r\n',b'\n'),0o100755)
    put('bin/video-runtime-framework-boot',(ROOT/'probe/test2/video-runtime-framework-boot').read_bytes().replace(b'\r\n',b'\n'),0o100755)
    mode,init=before['init'];anchor=b'lockscreen-framework-boot > /logs/lockscreen-framework-boot.txt 2>&1 &\n'
    if init.count(anchor)!=1:raise ValueError('init anchor mismatch')
    put('init',init.replace(anchor,anchor+b'    video-runtime-framework-boot > /logs/video-runtime-boot.txt 2>&1 &\n'),mode)
    rest=base;credentials=None
    while rest:
        dec=zlib.decompressobj(31);payload=dec.decompress(rest)
        if not dec.eof:raise ValueError('truncated gzip')
        entries=c.entries(payload)
        if 'etc/shadow' in entries and 'bin/dropbear' in entries:credentials=rest[:len(rest)-len(dec.unused_data)]
        rest=dec.unused_data
    if credentials is None:raise ValueError('missing private SSH member')
    archive=b''.join(c.entry(n,b,m) for n,(m,b) in delta.items())+c.entry('TRAILER!!!',b'',0)
    archive+=b'\0'*(-len(archive)%512)
    result=base+gzip.compress(archive,mtime=0)+credentials
    after=c.entries(gzip.decompress(result))
    changed={n for n in before.keys()|after.keys() if before.get(n)!=after.get(n)}
    assert changed==set(delta)
    assert all(after[n]==v for n,v in delta.items())
    assert all(after[n]==v for n,v in before.items() if n not in delta)
    with a.output.open('xb') as f:f.write(result)
    report=dict(sha256=sha(result),changed=sorted(changed),credentials_last=True,boot_tested=False,
                requires='sq2.video.c2=experimental sq2.video.autostart=1')
    a.output.with_suffix('.audit.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
if __name__=='__main__':main()
