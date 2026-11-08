#!/usr/bin/env python3
"""Add paired HIDL OMX services to an explicit 6.18 parent, preserving credentials."""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path, PurePosixPath
import subprocess
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('pack', ROOT/'scripts/pack-sq2-lts-audio-ssam.py')
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)
sha = lambda b: hashlib.sha256(b).hexdigest()

def verified_files(directory):
    files = {}
    for line in (directory/'SHA256SUMS').read_text().splitlines():
        digest, name = line.split(None, 1)
        path = PurePosixPath(name)
        if path.is_absolute() or '..' in path.parts or name in files:
            raise ValueError('Invalid manifest member')
        source = directory/name
        if source.is_symlink() or sha(source.read_bytes()) != digest:
            raise ValueError('Input checksum mismatch: ' + name)
        files[name] = source.read_bytes()
    files['SHA256SUMS'] = (directory/'SHA256SUMS').read_bytes()
    assert {f.relative_to(directory).as_posix() for f in directory.rglob('*') if f.is_file()} == set(files)
    return files

def strip_c2(base):
    """Remove only our retired C2 records, preserving every other newc header."""
    raw = gzip.decompress(base)
    kept, removed, offset = [], set(), 0
    prefixes = ('etc/sq2-c2-runtime-boot', 'etc/sq2-c2-experimental')
    while offset < len(raw):
        if raw[offset] == 0:
            end = offset + 1
            while end < len(raw) and raw[end] == 0:
                end += 1
            kept.append(raw[offset:end]); offset = end
            continue
        if raw[offset:offset+6] != b'070701':
            raise ValueError('Expected newc archive')
        header = raw[offset:offset+110]
        size = int(header[54:62], 16)
        namesize = int(header[94:102], 16)
        name_end = offset + 110 + namesize
        if namesize < 1 or name_end > len(raw) or raw[name_end-1] != 0:
            raise ValueError('Invalid newc name')
        name = raw[offset+110:name_end-1].decode()
        end = ((name_end + 3) & ~3) + size
        end = (end + 3) & ~3
        if end > len(raw):
            raise ValueError('Truncated newc payload')
        if name == 'bin/video-codec-manifest-stage' or any(name == p or name.startswith(p+'/') for p in prefixes):
            removed.add(name)
        else:
            kept.append(raw[offset:end])
        offset = end
    return gzip.compress(b''.join(kept), mtime=0), removed

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('base', 'hidl', 'output'):
        parser.add_argument(name, type=Path)
    parser.add_argument('--expected-base-sha256', required=True)
    args = parser.parse_args()
    base = args.base.read_bytes()
    assert sha(base) == args.expected_base_sha256
    before = pack.cpio.entries(gzip.decompress(base))
    assert any(n.startswith('lib/modules/6.18.60-sq2-android1/') for n in before)
    hidl = verified_files(args.hidl)
    assert b'0x01000000' in (ROOT/'probe/video/venus-sq2-omx-flags.h').read_bytes()
    changes = {}
    def put(name, data, mode=0o100644):
        for parent in reversed(PurePosixPath(name).parents):
            directory = str(parent)
            if directory != '.' and directory not in before and directory not in changes:
                changes[directory] = (0o040755, b'')
        changes[name] = (mode, data)
    for name, data in hidl.items():
        put('etc/sq2-hidl/'+name, data, 0o100755 if name in ('hwservicemanager','quest2-omx-service') else 0o100644)
    helper = (ROOT/'probe/test2/quest2-hidl-stage').read_bytes()
    subprocess.run(['sh','-n'],input=helper,check=True)
    put('bin/quest2-hidl-stage',helper,0o100755)
    anchor = b'video-codec-manifest-stage > /logs/video-c2-stage.txt 2>&1 || fail video-c2-stage\n'
    init = before['init'][1]
    assert init.count(anchor) == 1
    put('init',init.replace(anchor,b'quest2-hidl-stage > /logs/hidl-stage.txt 2>&1 || fail hidl-stage\n'),before['init'][0])
    prop = 'etc/googlebook-security/build.prop'
    old = before[prop][1]
    assert b'debug.stagefright.ccodec=4' in old
    put(prop,old.replace(b'debug.stagefright.ccodec=4',b'debug.stagefright.ccodec=1'),before[prop][0])
    runtime_helper = 'bin/video-runtime-framework-boot'
    old = before[runtime_helper][1]
    anchor = b'exec /bin/video-decoder-lab-start'
    assert old.count(anchor) == 1
    # Refresh codec enumeration once the real native backend has started.
    new = old.replace(anchor,b'/bin/video-decoder-lab-start\n'
        b'grep -q "sys_error=0 core_state=1" /sys/module/venus_core/parameters/sq2_pm_status\n'
        b'timeout 5 nsenter -t 1 -m -r -- /system/bin/setprop ctl.restart media')
    wait = b'test "$(timeout 3 nsenter -t 1 -m -r -- /system/bin/getprop init.svc.sq2-c2-experimental)" = running\n'
    assert new.count(wait) == 1
    new = new.replace(wait,b'')
    refresh = b'timeout 5 nsenter -t 1 -m -r -- /system/bin/setprop ctl.restart media'
    configure = (b'nsenter -t 1 -m -r -- /system/bin/sh <<\'ANDROID\'\n'
        b'set -eu\n'
        b'LD_LIBRARY_PATH=/vendor/etc/sq2-omx/lib64:/system/lib64:/vendor/lib64 /vendor/bin/hw/quest2-omx-service --print-capabilities-xml > /vendor/etc/media_codecs_quest2_omx.xml.next\n'
        b'mv /vendor/etc/media_codecs_quest2_omx.xml.next /vendor/etc/media_codecs_quest2_omx.xml\n'
        b'cp /vendor/etc/media_codecs_quest2_omx.xml /vendor/etc/sq2-omx/media_codecs.xml\n'
        b'setprop ctl.start sq2-omx\n'
        b'ANDROID\n'
        b'for n in $(seq 1 30); do\n'
        b' [ "$(timeout 3 nsenter -t 1 -m -r -- /system/bin/getprop init.svc.sq2-omx)" != running ] || break\n'
        b' sleep 1\n'
        b'done\n'
        b'test "$(timeout 3 nsenter -t 1 -m -r -- /system/bin/getprop init.svc.sq2-omx)" = running\n')
    new = new.replace(refresh,configure+refresh)
    put(runtime_helper,new,before[runtime_helper][0])
    for name in ('init',runtime_helper):
        subprocess.run(['sh','-n'],input=changes[name][1],check=True)
    raw = b''.join(pack.cpio.entry(n,b,m) for n,(m,b) in changes.items()) + pack.cpio.entry('TRAILER!!!',b'',0)
    raw += b'\0' * (-len(raw)%512)
    cleaned, removed = strip_c2(base)
    result = cleaned + gzip.compress(raw,mtime=0) + pack.credential_tail(base)
    expected = {n: v for n, v in before.items() if n not in removed} | changes
    assert pack.cpio.entries(gzip.decompress(result)) == expected
    args.output.parent.mkdir(parents=True,exist_ok=True)
    with args.output.open('xb') as target:
        target.write(result)
    report = dict(base_sha256=sha(base),sha256=sha(result),bytes=len(result),
                  changed_entries=sorted(changes),credentials_unchanged=True,
                  removed_entries=sorted(removed), c2_runtime=None,deployed=False,boot_verified=False)
    args.output.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
if __name__ == '__main__':
    main()
