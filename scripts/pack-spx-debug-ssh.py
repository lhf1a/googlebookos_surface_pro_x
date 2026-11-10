#!/usr/bin/env python3
"""Temporary early SSH on the current universal image; no device writes/hash gates.

Normal full-image policy is not changed. Reuse existing Wi-Fi/SSH credentials.
The diagnostic boot keeps bootstrap Wi-Fi across Android PID1 handoff rather
than waiting for sys.boot_completed or shutting it down before GPU failure.
"""
import argparse
import gzip
import json
from pathlib import Path
import subprocess

from disk_image import module, require
from full_image import new_record, records_from_gzip

ROOT=Path(__file__).resolve().parents[1]


def once(body,old,new):
    require(body.count(old)==1,'Unknown diagnostic init anchor: '+old.decode())
    return body.replace(old,new,1)


def transform(bodies):
    required={'init','bin/network-up','bin/start-ssh','bin/dropbear','bin/wpa_supplicant',
              'bin/wpa_cli','bin/dhcp-hook','bin/wifi-services','etc/wpa_supplicant.conf',
              'etc/shadow','etc/dropbear/dropbear_ed25519_host_key'}
    require(required<=bodies.keys(),'Missing existing network/SSH components')
    require('etc/sq2-early-network.enabled' not in bodies,'Diagnostic marker already present')
    init=bodies['init']
    anchor=b'if [ "$rescue" = 1 ] || [ "$smoke" = 1 ] || [ "$ssd_smoke" = 1 ]; then debug=1; fi\n'
    init=once(init,anchor,anchor+b'''# SPX_TEMPORARY_EARLY_SSH: candidate only; normal image is unchanged.
early_ssh=0
if [ -f /etc/sq2-early-network.enabled ] && grep -qw 'sq2.el2=1' /proc/cmdline &&
   ! grep -qw 'sq2.ssh=0' /proc/cmdline; then early_ssh=1; fi
''')
    anchor=b'''    if [ "$debug" = 1 ]; then
        echo 'Preparing Wi-Fi and diagnostic SSH (maximum 120 seconds)...'
'''
    init=once(init,anchor,b'''    if [ "$early_ssh" = 1 ]; then
        echo 'Preparing temporary early SSH before Android/GPU startup...'
        timeout 240 spx-early-ssh start > /logs/early-network.txt 2>&1 || echo 'SPX_EARLY_SSH_PENDING: see /logs/early-network.txt'
        touch-services > /logs/touch-startup.txt 2>&1 &
    elif [ "$debug" = 1 ]; then
        echo 'Preparing Wi-Fi and diagnostic SSH (maximum 120 seconds)...'
''')
    anchor=b'qualcomm-full-start > /logs/qualcomm-full.txt 2>&1 || fail qualcomm-full-graphics\n'
    init=once(init,anchor,b'''if [ "$early_ssh" = 1 ]; then
    spx-early-ssh stage > /logs/early-ssh-ownership.txt 2>&1 || fail early-ssh-ownership
fi
'''+anchor)
    init=once(init,b'    if [ "$debug" = 0 ] && ! grep -q \'sq2.ssh=0\' /proc/cmdline; then\n',
              b'    if [ "$early_ssh" = 0 ] && [ "$debug" = 0 ] && ! grep -q \'sq2.ssh=0\' /proc/cmdline; then\n')
    init=once(init,b'if [ "$debug" = 1 ] && [ "$smoke" = 0 ] && [ "$ssd_smoke" = 0 ] &&\n',
              b'if [ "$early_ssh" = 0 ] && [ "$debug" = 1 ] && [ "$smoke" = 0 ] && [ "$ssd_smoke" = 0 ] &&\n')
    network=module('spx_debug_network','pack-sq2-lts-early-network.py').prepare_network_up(bodies['bin/network-up'])
    delta={'init':init,
           'bin/spx-early-ssh':(ROOT/'probe/test2/spx-early-ssh').read_bytes().replace(b'\r\n',b'\n'),
           'bin/el2-early-network-up':network,
           'bin/el2-early-dhcp-hook':(ROOT/'probe/test2/el2-early-dhcp-hook').read_bytes().replace(b'\r\n',b'\n'),
           'etc/sq2-early-network.enabled':b'temporary-debug-only\n'}
    for name,body in delta.items():
        if body.startswith(b'#!'):
            subprocess.run(['sh','-n'],input=body,check=True,capture_output=True)
    return delta


def build(base,output):
    require(base.resolve()!=output.resolve() and not output.exists(),'Use a fresh candidate path')
    records,bodies=records_from_gzip(base)
    delta=transform(bodies)
    encode=module('spx_debug_encode','pack-sq2-release.py').encode
    metadata=dict(records)
    for name,body in delta.items():
        if name not in metadata: metadata[name]=new_record(0o100755 if body.startswith(b'#!') else 0o100600)
    identity={'bin/dropbear','bin/start-ssh','etc/shadow'}
    require(identity<=bodies.keys(),'Missing existing final credential members')
    # Preserve all original compressed bytes and put original credentials last.
    def member(values):
        raw=b''.join(encode(name,body,metadata[name]) for name,body in values.items())
        raw+=encode('TRAILER!!!',b'',new_record(0))
        return gzip.compress(raw+b'\0'*(-len(raw)%512),mtime=0)
    output.parent.mkdir(parents=True,exist_ok=True)
    with base.open('rb') as source, output.open('xb') as target:
        import shutil
        shutil.copyfileobj(source,target,8*1024*1024)
        target.write(member(delta))
        target.write(member({name:bodies[name] for name in sorted(identity)}))
    report={'operation':'temporary-pre-framework-debug-ssh','base':str(base),'output':str(output),
            'bytes':output.stat().st_size,'changed_records':sorted(delta),
            'credentials_unchanged':True,'original_compressed_prefix_preserved':True,
            'waits_for_android_boot_completed':False,'wifi_handoff_before_gpu_failure':False,
            'temporary_android_wifi_ownership':'known Android Wi-Fi services withheld in RAM overlay; initramfs owns association/DHCP',
            'network':'existing private Wi-Fi configuration; DHCP address printed on console',
            'ssh':'existing root password/authorized keys and host identity; TCP22',
            'disable':'sq2.ssh=0 restores ordinary startup for this candidate',
            'content_hash_gates':False,'new_backups':False,'rollback_test':False,
            'device_written':False,'hardware_ssh_verified':False,'gpu_fixed':False}
    output.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('base',type=Path);p.add_argument('output',type=Path)
    args=p.parse_args()
    print(json.dumps(build(args.base,args.output),indent=2),flush=True)
