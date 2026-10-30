#!/usr/bin/env python3
"""Package the tested SDR runtime; no device install, registration or firmware start."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
SERVICE = '27161e41fb7aaba13a851ee14d9168da75568dc98b3ec21e2a14db954c602ee5'
MAPPER = 'eb31d4022c97e1ac70e4b396eeb7c5599d1f904730bbdd241704a6929bc967a8'
CLANG = '/root/.cache/sq2-graphics/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang'

def digest(blob):
    return hashlib.sha256(blob).hexdigest()

def package(service, mapper, output):
    if output.exists():
        raise ValueError('refusing existing output')
    blobs = {'service': service.read_bytes(), 'libminigbm_gralloc.so': mapper.read_bytes()}
    for name, expected in [('service', SERVICE), ('libminigbm_gralloc.so', MAPPER)]:
        if digest(blobs[name]) != expected:
            raise ValueError('unverified runtime: ' + name)
    xml = (ROOT/'configs/sq2-media-codecs-c2.xml').read_bytes()
    nodes = ET.fromstring(xml).findall('./Decoders/MediaCodec')
    expected = {'c2.sq2.'+c+'.experimental': 'OMX.qcom.video.decoder.'+c for c in ('avc','hevc','vp8','vp9')}
    if {n.attrib['name']: n.find('Alias').attrib['name'] for n in nodes} != expected or len(nodes) != 4:
        raise ValueError('codec name contract changed')
    blobs['media_codecs_c2.xml'] = xml
    with tempfile.TemporaryDirectory() as tmp:
        binary = Path(tmp)/'launch'
        subprocess.run([CLANG, '-O2', '-Wall', '-Wextra', '-Werror', str(ROOT/'probe/video/sq2-c2-runtime-launch.c'), '-o', str(binary)], check=True)
        blobs['launch'] = binary.read_bytes()
    blobs['start.sh'] = b'''#!/system/bin/sh
set -eu
case "${1:-}" in --serve|--names-test) ;; *) exit 2;; esac
test "$#" -eq 1
test "$(cat /sys/module/suspend/parameters/screen_only)" = Y
cd /data/vendor/sq2-codec2/v1
sha256sum -c SHA256SUMS
exec ./launch "$1"
'''
    # Audit metadata is explicit about what this package does not achieve.
    blobs['runtime.json'] = (json.dumps(dict(version=1, path='/data/vendor/sq2-codec2/v1',
        firmware_start=False, boot_integrated=False, hdr_testing=False,
        service_sha256=SERVICE, mapper_sha256=MAPPER), sort_keys=True, indent=2)+'\n').encode()
    blobs['SHA256SUMS'] = ''.join(digest(data)+'  '+name+'\n' for name,data in sorted(blobs.items())).encode()
    archive = io.BytesIO()
    with tarfile.open(fileobj=archive, mode='w') as tar:
        for name, data in sorted(blobs.items()):
            info = tarfile.TarInfo(name); info.size = len(data)
            info.mode = 0o755 if name in ('service','launch','start.sh') else 0o644
            tar.addfile(info, io.BytesIO(data))
    with tarfile.open(fileobj=io.BytesIO(archive.getvalue())) as tar:
        assert all(tar.extractfile(name).read() == data for name,data in blobs.items())
    with output.open('xb') as f:
        f.write(archive.getvalue())
    print(json.dumps(dict(path=str(output.resolve()), sha256=digest(output.read_bytes()), files=sorted(blobs)), indent=2))

if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('service', type=Path); p.add_argument('mapper', type=Path); p.add_argument('output', type=Path)
    a=p.parse_args(); package(a.service, a.mapper, a.output)
