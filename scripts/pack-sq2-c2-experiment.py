#!/usr/bin/env python3
"""Private boot candidate only. Never deploys; native backend is started manually."""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import zlib

ROOT = Path(__file__).resolve().parents[1]
BASE = '5ba59bd91c14208442a15792dedff236b0f1789c4d8fe10d00ee50f5e0a4953c'
SERVICE = '4e7d98a7cbb4621cfada47bacd9f75250d542aab40d1543df369d532ff640b89'
sha = lambda b: hashlib.sha256(b).hexdigest()

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('base', type=Path)
    p.add_argument('service_bundle', type=Path)
    p.add_argument('native_bundle', type=Path)
    p.add_argument('output', type=Path)
    a = p.parse_args()
    if a.output.exists():
        raise ValueError('Output exists')
    spec = importlib.util.spec_from_file_location('cpio', ROOT/'scripts/pack-sq2-vulkan.py')
    c = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(c)
    base = a.base.read_bytes()
    assert sha(base) == BASE
    before = c.entries(gzip.decompress(base))
    anchor = b'apply-device-info > /logs/device-info.txt 2>&1 || fail device-info\n'
    mode, init = before['init']
    assert init.count(anchor) == 1 and b'video-codec-manifest-stage' not in init
    delta = {}
    def put(name, data, mode=0o100644):
        assert name not in before and name not in delta
        parent = Path(name).parent.as_posix()
        if parent != '.' and parent not in before and parent not in delta:
            put(parent, b'', 0o40755)
        delta[name] = (mode, data)
    service = (a.service_bundle/'service').read_bytes()
    assert sha(service) == SERVICE
    rc = (ROOT/'configs/sq2-c2-experimental.rc').read_bytes().replace(b'\r\n', b'\n')
    prefix = 'etc/sq2-c2-experimental/'
    put(prefix+'service', service, 0o100755)
    put(prefix+'service.rc', rc)
    put(prefix+'SHA256SUMS', f'{sha(service)}  service\n{sha(rc)}  service.rc\n'.encode())
    manifest = (a.native_bundle/'SHA256SUMS').read_text()
    for line in manifest.splitlines():
        digest, name = line.split()
        assert Path(name).name == name and name not in ('.', '..')
        blob = (a.native_bundle/name).read_bytes()
        assert sha(blob) == digest
        put('etc/sq2-video-native/'+name, blob, 0o100755 if not name.endswith('.ko') else 0o100644)
    put('etc/sq2-video-native/SHA256SUMS', manifest.encode())
    for name in ['video-codec-manifest-stage', 'video-decoder-lab-start']:
        put('bin/'+name, (ROOT/'probe/test2'/name).read_bytes().replace(b'\r\n', b'\n'), 0o100755)
    hook = b'video-codec-manifest-stage > /logs/video-c2-stage.txt 2>&1 || fail video-c2-stage\n'
    delta['init'] = (mode, init.replace(anchor, hook+anchor))
    rest, credentials = base, None
    while rest:
        dec = zlib.decompressobj(31)
        payload = dec.decompress(rest)
        assert dec.eof
        files = c.entries(payload)
        if 'etc/shadow' in files and 'bin/dropbear' in files:
            credentials = rest[:len(rest)-len(dec.unused_data)]
        rest = dec.unused_data
    assert credentials, 'Missing password SSH addon'
    archive = b''.join(c.entry(n, data, mode) for n, (mode, data) in delta.items())
    archive += c.entry('TRAILER!!!', b'', 0)
    archive += b'\0' * (-len(archive) % 512)
    output = base + gzip.compress(archive, mtime=0) + credentials
    after = c.entries(gzip.decompress(output))
    assert {n for n in before.keys() | after.keys() if before.get(n) != after.get(n)} == set(delta)
    assert all(after[n] == before[n] for n in before if n != 'init')
    with a.output.open('xb') as f:
        f.write(output)
    report = dict(base_sha256=BASE, output_sha256=sha(output), changed=sorted(delta),
                  credentials_last=True, backend='manual-after-boot-with-bounded-collector',
                  opt_in='sq2.video.c2=experimental', boot_tested=False)
    a.output.with_suffix('.audit.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))

if __name__ == '__main__':
    main()
