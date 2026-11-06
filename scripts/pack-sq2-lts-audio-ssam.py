#!/usr/bin/env python3
"""Pack verified audio-provider/SSAM corrections into a private 6.18 initramfs.

Retains the input, credentials and last-wins entries; regenerates module indexes.
No device writes, service restarts, firmware resets or reboots are performed.
"""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path, PurePosixPath
import posixpath
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('cpio', ROOT/'scripts/pack-sq2-vulkan.py')
cpio = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cpio)
sha = lambda data: hashlib.sha256(data).hexdigest()


def run(args):
    p = subprocess.run(args, capture_output=True, text=True)
    print(json.dumps(dict(command=args, stdout=p.stdout, stderr=p.stderr,
                          exit=p.returncode)), flush=True)
    if p.returncode:
        raise RuntimeError('Command failed: '+repr(args))
    return p.stdout.strip()


def credential_tail(data):
    """Keep exact effective addon files, even if an earlier pack merged members.

    Never append a whole merged base as a credential addon: it would silently
    overwrite newer modules and SELinux policy. The three paths come from the
    project's pack-sq2-password-ssh.sh, not from a new credential provisioning.
    """
    effective = cpio.entries(gzip.decompress(data))
    paths = ('bin/dropbear', 'bin/start-ssh', 'etc/shadow')
    if 'etc/shadow' not in effective:
        return b''
    assert all(n in effective for n in paths), 'Incomplete private SSH payload'
    found = b''
    while data:
        dec = zlib.decompressobj(31)
        payload = dec.decompress(data)
        if not dec.eof:
            raise ValueError('Truncated gzip member')
        count = len(data)-len(dec.unused_data)
        files = cpio.entries(payload)
        regular = {n for n, (mode, _) in files.items() if mode & 0o170000 != 0o040000}
        if regular == set(paths) and all(files[n] == effective[n] for n in paths):
            found = data[:count]
        data = dec.unused_data
    if found:
        return found
    raw = b''.join(cpio.entry(n, effective[n][1], effective[n][0]) for n in paths)
    raw += cpio.entry('TRAILER!!!', b'', 0)
    raw += b'\0'*(-len(raw) % 512)
    return gzip.compress(raw, mtime=0)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('base', 'audio_helper', 'ssam_module', 'output'):
        p.add_argument(name, type=Path)
    p.add_argument('--expected-base-sha256', required=True)
    p.add_argument('--system-map', required=True, type=Path)
    p.add_argument('--preserve-lab-ssh', action='store_true')
    a = p.parse_args()
    assert not a.output.exists(), 'Output must be fresh'
    base = a.base.read_bytes()
    assert sha(base) == a.expected_base_sha256, 'Input hash mismatch'
    before = cpio.entries(gzip.decompress(base))
    tail = credential_tail(base)
    assert bool(tail) == a.preserve_lab_ssh, 'Explicit private SSH preservation required'
    helper = a.audio_helper.read_bytes()
    assert b'qcom,sc8180x-adsp-pas' in helper and b'firmware-name' in helper
    run(['sh', '-n', str(a.audio_helper)])
    module = a.ssam_module.read_bytes()
    assert module[:6] == b'\x7fELF\x02\x01' and int.from_bytes(module[18:20], 'little') == 183
    vm = run(['modinfo', '-F', 'vermagic', str(a.ssam_module)])
    release = vm.split()[0]
    assert release.startswith('6.18.') and '/' not in release
    assert 'Csurface,aggregator' in run(['modinfo', '-F', 'alias', str(a.ssam_module)])
    prefix = 'lib/modules/'+release+'/'
    paths = [n for n in before if n.endswith('/surface_aggregator.ko')]
    assert len(paths) == 1 and paths[0].startswith(prefix), paths
    changes = {'bin/audio-drivers-start': (before['bin/audio-drivers-start'][0], helper),
               paths[0]: (before[paths[0]][0], module)}
    with tempfile.TemporaryDirectory(prefix='sq2-618-module-index-') as td:
        stage = Path(td)
        for name, (mode, body) in before.items():
            if not name.startswith(prefix):
                continue
            assert '..' not in PurePosixPath(name).parts
            if mode & 0o170000 == 0o100000:
                target = stage/name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(body)
        assert run(['modinfo', '-F', 'vermagic', str(stage/paths[0])]) == vm
        (stage/paths[0]).write_bytes(module)
        run(['depmod', '-b', str(stage), '-e', '-F', str(a.system_map), release])
        for target in (stage/prefix).glob('modules.*'):
            name = target.relative_to(stage).as_posix()
            body = target.read_bytes()
            if name not in before or before[name][1] != body:
                changes[name] = (before.get(name, (0o100644, b''))[0], body)
        aliases = (stage/prefix/'modules.alias').read_text()
        assert 'Csurface,aggregator' in aliases
        deps = (stage/prefix/'modules.dep').read_text()
        assert any('/surface_aggregator.ko:' in l and '/crc-itu-t.ko' in l for l in deps.splitlines())
    # Refresh only checksum records whose targets really changed.
    for name, (mode, body) in before.items():
        if not (name.endswith('SHA256SUMS') or name.endswith('.sha256')):
            continue
        lines = []
        changed = False
        for line in body.decode().splitlines():
            parts = line.split(None, 1)
            if len(parts) == 2:
                target = posixpath.normpath(posixpath.join(posixpath.dirname(name), parts[1].lstrip('*'))).lstrip('/')
                if target in changes:
                    line = sha(changes[target][1])+'  '+parts[1].lstrip('*')
                    changed = True
            lines.append(line)
        if changed:
            changes[name] = (mode, ('\n'.join(lines)+'\n').encode())
    delta = b''.join(cpio.entry(n, body, mode) for n, (mode, body) in changes.items())
    delta += cpio.entry('TRAILER!!!', b'', 0)
    delta += b'\0'*(-len(delta) % 512)
    result = base+gzip.compress(delta, mtime=0)+tail
    after = cpio.entries(gzip.decompress(result))
    assert after == before | changes
    for name in ('init', 'etc/android-native-test-policy', 'bin/audio-framework-boot',
                 'etc/sq2-audio/audio-hal', 'bin/resource-monitor-start', 'etc/shadow'):
        assert before.get(name) == after.get(name), name
    a.output.parent.mkdir(parents=True, exist_ok=True)
    with a.output.open('xb') as f:
        f.write(result)
    assert a.output.read_bytes() == result and a.base.read_bytes() == base
    report = dict(baseline_sha256=sha(base), sha256=sha(result), bytes=len(result),
                  changed_entries=sorted(changes), module_sha256=sha(module),
                  helper_sha256=sha(helper), kernel_release=release,
                  password_addon_last=bool(tail), private_package=True,
                  deployed=False, reboot_verified=False)
    a.output.with_suffix('.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
