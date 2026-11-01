#!/usr/bin/env python3
"""Build-only Android launcher using the paired EL1 preboot/EL2 RAM candidate.

Preserves the normal video flags; never installs, arms or reboots a device.
Matching payload hashes do not validate Wi-Fi, video, KSU or Terminal runtime.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import libfdt

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--ram-candidate', type=Path, required=True)
parser.add_argument('--android-payload', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
ram = args.ram_candidate.resolve()
payload = args.android_payload.resolve()
out = args.output.resolve()
audit = json.loads((payload / 'pack-audit.json').read_text())
assert digest(payload / 'android-el2.cpio.gz') == audit['sha256']
assert digest(ram / 'Image') == audit['kernel_sha256'], 'kernel/payload build mismatch'
assert audit['credential_last'] and audit['video_userspace_firmware_preserved']
assert len(audit['video_modules_replaced']) == 11
for line in (ram / 'SHA256SUMS').read_text().splitlines():
    sha, name = line.split()
    assert Path(name).name == name and digest(ram / name) == sha, name
out.mkdir(parents=True, exist_ok=False)
for name in ['Image', 'qebspilaa64.efi', 'tcblaunch.exe']:
    shutil.copyfile(ram / name, out / name)
shutil.copyfile(ram / 'preboot-el2.dtb', out / 'android-el2.dtb')
# Do not inherit temporary RAM-only resource-vote experiments into Android.
dt = libfdt.Fdt((out / 'android-el2.dtb').read_bytes())
node = dt.path_offset('/soc@0/remoteproc@4080000')
try:
    dt.delprop(node, 'microsoft,el2-modem-proxy-test')
except libfdt.FdtException as exc:
    if exc.err != -libfdt.NOTFOUND:
        raise
dt.pack()
(out / 'android-el2.dtb').write_bytes(bytes(dt.as_bytearray()))
shutil.copyfile(payload / 'android-el2.cpio.gz', out / 'initramfs.cpio.gz')
shutil.copytree(ram / 'firmware', out / 'firmware')
shutil.copytree(ram / 'efi-source', out / 'efi-source', symlinks=True,
                ignore=shutil.ignore_patterns('out'))
source = out / 'efi-source/src/bounce_main.c'
text = source.read_text().replace('SQ2EL2Preboot', 'SQ2EL2Android')
text = text.replace('preboot-el2.dtb', 'android-el2.dtb')
start = text.index('    static CHAR16 options[] =')
end = text.index('\n', start)
options = ('console=tty0 earlycon=efifb loglevel=6 acpi=off efi=novamap '
           'clk_ignore_unused pd_ignore_unused suspend.screen_only=1 nohibernate '
           'panic=15 sq2.native=ssd sq2.storage=nvme sq2.el2=experimental '
           'sq2.audio=1 sq2.video.c2=experimental sq2.video.autostart=1 '
           'sq2.vulkan=mesa sq2.display=2.7-60 sq2.usb=experimental '
           'nvme_core.default_ps_max_latency_us=0 lsm=selinux enforcing=0 '
           'printk.devkmsg=on sq2.el2_trace=1 '
           r'initrd=\\EFI\\SQ2EL2Android\\initramfs.cpio.gz')
text = text[:start] + '    static CHAR16 options[] = L"' + options + '";' + text[end:]
text = text.replace('SQ2 EL2 preboot diagnostic. RAM only; known ESP logs; automatic normal reboot.',
                    'SQ2 Android EL2 preboot candidate. Original default retained.')
assert 'sq2.video=0' not in text and 'rdinit=/init' not in text
assert 'sq2_progress_mark(0x31' in text and 'preboot prepare' in text
source.write_text(text)
with (out / 'build.log').open('w') as log:
    subprocess.run(['make', '-s', '-C', str(out / 'efi-source'), '-j8'],
                   stdout=log, stderr=subprocess.STDOUT, check=True)
shutil.copyfile(out / 'efi-source/out/slbounce.efi', out / 'el2launch.efi')
names = ['Image', 'android-el2.dtb', 'initramfs.cpio.gz', 'qebspilaa64.efi',
         'tcblaunch.exe', 'el2launch.efi']
(out / 'SHA256SUMS').write_text(''.join(f'{digest(out/n)}  {n}\n' for n in names))
(out / 'audit.json').write_text(json.dumps({
    'kernel_sha256': digest(out/'Image'), 'payload_sha256': audit['sha256'],
    'cmdline_c_literal': options, 'hardware_tested': False,
    'vpu_el2_lifecycle_verified': False, 'installed': False,
}, indent=2) + '\n')
print('BUILD ONLY:', out, '; full device/Terminal validation still required')
