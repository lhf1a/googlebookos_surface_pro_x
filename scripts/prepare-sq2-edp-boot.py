#!/usr/bin/env python3
"""Prepare one-shot GRUB files locally; no disk writes or device connection."""
from pathlib import Path
import hashlib, subprocess, json, re
root=Path(__file__).resolve().parents[1]
out=root/'out/display-refresh'
base=(root/'out/quest2-gpu/boot-runtime-v8.cpio.gz').read_bytes()
combined=(out/'native-edp-rates-v3.cpio.gz').read_bytes()
assert combined.startswith(base)
assert hashlib.sha256(combined).hexdigest()==json.loads((out/'native-edp-rates-v3.cpio.audit.json').read_text())['output_sha256']
addon=out/'edp-rates-v3-addon.cpio.gz'
if not addon.exists(): addon.write_bytes(combined[len(base):])
assert addon.read_bytes()==combined[len(base):]
original=(out/'grub-before.cfg').read_text()
assert original.count('set default=sq2-internal')==1 and 'sq2-edp-test' not in original
block='''
# Consume the test request before loading any candidate code.
search --no-floppy --fs-uuid --set=root B027-0512
if [ -s /EFI/BOOT/sq2-edp-test.env ]; then
    load_env -f /EFI/BOOT/sq2-edp-test.env sq2_edp_once
    if [ "$sq2_edp_once" = "1" ]; then
        set sq2_edp_once=0
        save_env -f /EFI/BOOT/sq2-edp-test.env sq2_edp_once
        set default=sq2-edp-test
    fi
fi
'''
start=original.index("menuentry '1.")
end=original.index('\n}',start)+2
entry=original[start:end]
entry=entry.replace("'1. Googlebook OS - SQ2 NVMe + audio + closed Vulkan' --id sq2-internal", "'3. SQ2 eDP 5.4Gbps test - 60Hz' --id sq2-edp-test")
assert '--id sq2-edp-test' in entry
entry=entry.replace('sq2.audio=1 ', 'sq2.audio=1 sq2.edp_test=1 ')
entry=entry.replace('initrd /boot/sq2-android1/native-closed-vulkan-v8.cpio.gz', 'initrd /boot/sq2-android1/native-closed-vulkan-v8.cpio.gz /boot/sq2-android1/edp-rates-v3-addon.cpio.gz')
menus=re.findall(r'^menuentry [^\n]+\{\n.*?^\}',original,re.M|re.S)
closed=next(m for m in menus if '--id sq2-internal {' in m)
mesa=next(m for m in menus if '--id sq2-mesa-fallback {' in m)
mesa=mesa.replace("'4. SQ2 original Mesa - graphical fallback'", "'2. SQ2 original Mesa'")
header=original[:original.index('menuentry')]
cfg=header+block+'\n'+closed+'\n\n'+mesa+'\n\n'+entry+'\n'
assert cfg.count('menuentry ')==3 and 'sq2.debug=1' not in cfg
(out/'grub-edp-test.cfg').write_text(cfg)
subprocess.run(['grub-script-check',str(out/'grub-edp-test.cfg')],check=True)
env=out/'sq2-edp-test.env'
if not env.exists(): subprocess.run(['grub-editenv',str(env),'create'],check=True)
subprocess.run(['grub-editenv',str(env),'set','sq2_edp_once=1'],check=True)
for p in [addon,out/'grub-before.cfg',out/'grub-edp-test.cfg',env]:
 print(p.name, p.stat().st_size, hashlib.sha256(p.read_bytes()).hexdigest())
