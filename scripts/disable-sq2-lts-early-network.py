#!/usr/bin/env python3
"""Restore pre-early-network initramfs plus independent boot logging.

Requires the exact preserved pre-addon prefix; credentials, modules and normal
Android/post-boot SSH policy are unchanged. No device access or reboot.
"""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('cpio', ROOT/'scripts/pack-sq2-vulkan.py')
cpio = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cpio)


def build(current, before_early):
    assert current.startswith(before_early), 'Pre-addon archive is not the exact original prefix'
    active = cpio.entries(gzip.decompress(current))
    pristine = cpio.entries(gzip.decompress(before_early))
    addon = {'bin/el2-early-network', 'bin/el2-early-network-up',
             'bin/el2-early-dhcp-hook', 'etc/sq2-early-network.enabled'}
    assert set(active)-set(pristine) == addon
    assert set(pristine)-set(active) == set()
    for name in pristine:
        if name != 'init':
            assert pristine[name] == active[name], name
    mode, init = pristine['init']
    assert b'el2-early-network' not in init
    anchor = b'if [ "$debug" = 1 ]; then\nmkdir -p "$logdir"'
    assert init.count(anchor) == 1
    init = init.replace(anchor, b'if [ "$debug" = 1 ] || [ -f /etc/sq2-boot-log.enabled ]; then\nmkdir -p "$logdir"', 1)
    subprocess.run(['sh', '-n'], input=init, check=True)
    delta = {'init': (mode, init),
             'etc/sq2-boot-log.enabled': (0o100600, b'private-boot-log-only\n')}
    raw = b''.join(cpio.entry(n, b, m) for n, (m, b) in delta.items())
    raw += cpio.entry('TRAILER!!!', b'', 0)
    candidate = before_early + gzip.compress(raw, mtime=0)
    after = cpio.entries(gzip.decompress(candidate))
    assert after == dict(pristine, **delta)
    assert not addon.intersection(after)
    assert b'ssh-framework-boot >' in after['init'][1]
    return candidate


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('current', type=Path)
    p.add_argument('before_early', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--expected-current-sha256', required=True)
    a = p.parse_args()
    assert not a.output.exists()
    current = a.current.read_bytes()
    before = a.before_early.read_bytes()
    sha = lambda b: hashlib.sha256(b).hexdigest()
    assert sha(current) == a.expected_current_sha256
    candidate = build(current, before)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    with a.output.open('xb') as f:
        f.write(candidate)
    assert a.output.read_bytes() == candidate
    assert a.current.read_bytes() == current and a.before_early.read_bytes() == before
    report = {'baseline_sha256': sha(current), 'pre_addon_sha256': sha(before),
              'sha256': sha(candidate), 'bytes': len(candidate),
              'early_association_dhcp_ssh': False, 'normal_android_wifi': 'preserved',
              'post_boot_ssh': 'preserved', 'persistent_boot_log': True,
              'credentials_modules_firmware_unchanged': True, 'deployed': False}
    a.output.with_suffix('.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
