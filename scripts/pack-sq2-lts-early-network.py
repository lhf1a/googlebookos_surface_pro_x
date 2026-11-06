#!/usr/bin/env python3
"""Append private early EL2 Wi-Fi/SSH diagnostics; preserve the exact base archive.

No device access, credential generation, backend switch, firmware restart or reboot.
The opt-in is in this candidate only, not the standard boot policy.
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


def once(data, old, new):
    assert data.count(old) == 1, f'Unexpected source anchor: {old!r}'
    return data.replace(old, new, 1)


def prepare_network_up(network):
    network = network.replace(b'/run/wpa_supplicant', b'/run/sq2-early-wpa')
    network = once(network, b'ip link set "$dev" up\n',
                   b'echo "$dev" > /run/sq2-early-network.iface\nip link set "$dev" up\n')
    network = once(network, b'wpa_supplicant -B -D nl80211',
                   b'wpa_supplicant -B -P /run/sq2-early-wpa.pid -O /run/sq2-early-wpa -D nl80211')
    network = once(network, b'pidfile=/run/udhcpc-$dev.pid', b'pidfile=/run/sq2-early-udhcpc.pid')
    network = once(network, b'-s /bin/dhcp-hook', b'-s /bin/el2-early-dhcp-hook')
    return once(network, b'echo "$dev" > /run/ssh-interface',
                b'echo "$dev" > /run/sq2-early-network.iface')


def transform(entries):
    for name in ('init', 'bin/network-up', 'bin/start-ssh', 'bin/wifi-services',
                 'bin/boot-kmsg', 'bin/dropbear', 'bin/wpa_supplicant', 'bin/wpa_cli',
                 'bin/dhcp-hook', 'etc/wpa_supplicant.conf',
                 'etc/dropbear/dropbear_ed25519_host_key', 'root/.ssh/authorized_keys'):
        assert name in entries, name
    assert 'etc/sq2-early-network.enabled' not in entries, 'Already patched'
    init = entries['init'][1]
    anchor = b'if [ "$rescue" = 1 ] || [ "$smoke" = 1 ] || [ "$ssd_smoke" = 1 ]; then debug=1; fi\n'
    init = once(init, anchor, anchor + b'''# SQ2_LTS_EARLY_NETWORK: local diagnostic candidate, not normal build policy.
early_network=0
if [ -f /etc/sq2-early-network.enabled ] && [ "$debug" = 0 ] &&
   grep -qw 'sq2.el2=1' /proc/cmdline && ! grep -qw 'sq2.ssh=0' /proc/cmdline; then
    early_network=1
fi
''')
    init = once(init, b'if [ "$debug" = 1 ]; then\nmkdir -p "$logdir"',
                b'if [ "$debug" = 1 ] || [ "$early_network" = 1 ]; then\nmkdir -p "$logdir"')
    anchor = b'''    if [ "$debug" = 1 ]; then
        echo 'Preparing Wi-Fi and diagnostic SSH (maximum 120 seconds)...'
'''
    init = once(init, anchor, b'''    if [ "$early_network" = 1 ]; then
        el2-early-network start > /logs/early-network.txt 2>&1 || echo 'SQ2_EARLY_NETWORK_PENDING: see early-network.txt'
        touch-services > /logs/touch-startup.txt 2>&1 &
    elif [ "$debug" = 1 ]; then
        echo 'Preparing Wi-Fi and diagnostic SSH (maximum 120 seconds)...'
''')
    init = once(init, b'sync\nexec native-handoff /newroot\n', b'''if [ "$early_network" = 1 ]; then
    el2-early-network handoff >> /logs/early-network.txt 2>&1 || fail early-network-ownership
fi
sync
exec native-handoff /newroot
''')
    network = prepare_network_up(entries['bin/network-up'][1])
    helper = (ROOT/'probe/test2/el2-early-network').read_bytes().replace(b'\r\n', b'\n')
    dhcp = (ROOT/'probe/test2/el2-early-dhcp-hook').read_bytes().replace(b'\r\n', b'\n')
    delta = {'init': (entries['init'][0], init),
             'bin/el2-early-network': (0o100755, helper),
             'bin/el2-early-network-up': (0o100755, network),
             'bin/el2-early-dhcp-hook': (0o100755, dhcp),
             'etc/sq2-early-network.enabled': (0o100600, b'private-diagnostic-only\n')}
    for name, (_, data) in delta.items():
        if data.startswith(b'#!'):
            subprocess.run(['sh', '-n'], input=data, check=True)
    assert init.index(b'el2-early-network start') < init.index(b'el2-terminal-stage >')
    assert init.index(b'el2-early-network handoff') < init.index(b'exec native-handoff')
    return delta


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('base', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--expected-base-sha256', required=True)
    a = p.parse_args()
    assert a.base.resolve() != a.output.resolve() and not a.output.exists()
    base = a.base.read_bytes()
    sha = lambda b: hashlib.sha256(b).hexdigest()
    assert sha(base) == a.expected_base_sha256
    before = cpio.entries(gzip.decompress(base))
    delta = transform(before)
    archive = b''.join(cpio.entry(n, data, mode) for n, (mode, data) in delta.items())
    archive += cpio.entry('TRAILER!!!', b'', 0)
    result = base + gzip.compress(archive, mtime=0)
    after = cpio.entries(gzip.decompress(result))
    assert after == dict(before, **delta), 'Unexpected effective entry or mode change'
    a.output.parent.mkdir(parents=True, exist_ok=True)
    with a.output.open('xb') as f:
        f.write(result)
    assert a.output.read_bytes() == result
    assert a.base.read_bytes() == base
    report = {'base_sha256': sha(base), 'sha256': sha(result), 'bytes': len(result),
              'changed_entries': list(delta), 'unchanged_entries': len(before)-1,
              'base_compressed_prefix_preserved': True, 'private_only': True,
              'deployed': False, 'hardware_verified': False,
              'handoff': 'bootstrap clients stop before Android; SSH listener retained',
              'routing': 'DHCP LAN address, not guaranteed overlay 10.126.126.1'}
    a.output.with_suffix(a.output.suffix+'.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
