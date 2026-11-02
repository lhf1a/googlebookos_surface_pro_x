"""Audit real candidate archives: exact delta, boot isolation and integrity."""
import gzip
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

R = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, R/'scripts'/name)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


b = load('build-sq2-default.py')
c = load('pack-sq2-vulkan.py')
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('output', type=Path)
p.add_argument('--baseline', type=Path, default=R/'out/default-input/initramfs.cpio.gz')
args = p.parse_args()
out = args.output
baseline = c.entries(gzip.decompress(args.baseline.read_bytes()))
report = json.loads((out/'build-audit.json').read_text())
usb = load('sq2-usb-network-boot-payload.py')
usb_enabled = report.get('usb_network',{}).get('enabled',False)
usb_allowed = set()
if usb_enabled:
    policy = usb.pins(R)
    usb_allowed = {usb.PREFIX+n for n in policy['modules']} | {usb.PREFIX+n for n in policy['metadata']}
    usb_allowed |= {'bin/'+n for n in usb.HELPERS} | {'etc/sq2-usb-network.aliases'}
    for name in list(usb_allowed):
        usb_allowed |= {p.as_posix() for p in Path(name).parents if p.as_posix() != '.'}
audio = json.loads((R/'configs/sq2-default-inputs.json').read_text())['audio_modules']
for directory, graphics in [('SQ2Default', 'closed'), ('SQ2Fallback', 'mesa')]:
    raw = (out/'EFI'/directory/'initramfs.cpio.gz').read_bytes()
    e = c.entries(gzip.decompress(raw))
    init = e['init'][1]
    assert b'sq2.el2=1' in init and b'el2-terminal-autostart 10 ' in init
    assert all(n not in init for n in (b'el2-android-recovery', b'el2-kmsg-collect', b'el2-logs-bind'))
    assert b'modprobe fuse' in init
    assert e['etc/shadow'] == baseline['etc/shadow']
    assert e['bin/dropbear'] == baseline['bin/dropbear']
    headers_name = 'lib/modules/6.3.8-sq2-android1/kernel/kernel/kheaders.ko'
    assert e[headers_name] == baseline[headers_name]
    assert set(baseline) <= set(e), 'Removed archive entries'
    assert set(e)-set(baseline) <= usb_allowed, 'Unexpected added archive entries'
    if usb_enabled:
        assert usb.verify(e,R) == report['usb_network']['source_sha256']
        for name in set(e)-set(baseline):
            if name not in {usb.PREFIX+n for n in policy['modules']} | {'bin/'+n for n in usb.HELPERS} | {'etc/sq2-usb-network.aliases'}:
                assert e[name] == (0o40755,b''), 'Unexpected directory payload'
    assert b'running|attached)' in e['bin/audio-drivers-start'][1]
    assert b'*" sq2.el2=1 "*)' in e['bin/video-decoder-lab-start'][1]
    for helper in ('el2-terminal-autostart', 'el2-terminal-stage'):
        assert e['bin/'+helper][1] == (R/'probe/virtualization'/helper).read_bytes().replace(b'\r\n', b'\n')
    for prefix in ('lib/firmware/', 'etc/sq2-c2-runtime-boot/', 'etc/sq2-audio/', 'etc/sq2-vulkan-closed/'):
        assert all(e[n] == value for n, value in baseline.items() if n.startswith(prefix)), prefix
    for line in e['etc/sq2-video-native/SHA256SUMS'][1].decode().splitlines():
        digest, name = line.split()
        assert hashlib.sha256(e['etc/sq2-video-native/'+name][1]).hexdigest() == digest
    # No unrelated effective changes relative to the working fallback.
    changed = {n for n in e if e[n] != baseline.get(n)}
    allowed = {'init', 'bin/audio-drivers-start', 'bin/video-decoder-lab-start',
               'bin/el2-rmtfs-start', 'bin/el2-terminal-stage', 'bin/el2-terminal-autostart'}
    allowed |= audio.keys()
    allowed |= usb_allowed
    for name, expected in audio.items():
        assert hashlib.sha256(e[name][1]).hexdigest() == expected, name
    assert changed <= allowed, changed
    text = (out/('source-'+directory)/'src/bounce_main.c').read_text()
    assert 'sq2.vulkan='+graphics in text and 'sq2.el2=1' in text
    assert 'suspend.screen_only=1 nohibernate' in text and 'sq2.display=2.7-60' in text
    assert 'sq2.el2_trace=' not in text and 'SQ2EL2Android' not in text
    assert directory in text
menu = (out/'grub.cfg').read_text()
assert (out/'EFI/SQ2Default/initramfs.cpio.gz').read_bytes() == (out/'EFI/SQ2Fallback/initramfs.cpio.gz').read_bytes()
assert menu == b.menu() and menu.count('menuentry ') == 2
assert 'set default=sq2-default' in menu
assert 'load_env' not in menu and 'save_env' not in menu
legacy = subprocess.run([sys.executable, str(R/'scripts/build-source-disk.py'), '--boot-dir', str(out)],
                        capture_output=True, text=True)
assert legacy.returncode == 2 and 'device-specific 512GB EL2 bundle' in legacy.stderr
print('PASS: real two-entry bundle, unchanged runtime/media/credentials, normal hooks and isolated launchers')
