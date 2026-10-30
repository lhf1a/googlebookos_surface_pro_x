#!/usr/bin/env python3
"""Generate a bounded, identity-checked stage script. Does not connect or reboot."""
from pathlib import Path
import hashlib, sys, re
root=Path(__file__).resolve().parents[1]
out=root/'out/display-refresh'
rev,previous=int(sys.argv[1]),int(sys.argv[2])
old_cfg=sys.argv[3]
assert 3 <= previous < rev <= 20 and re.fullmatch('[0-9a-f]{64}',old_cfg)
digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
addon=f'edp-rates-v{rev}-addon.cpio.gz'; old=f'edp-rates-v{previous}-addon.cpio.gz'
cfg=(out/'grub-edp-test.cfg').read_text()
assert cfg.count('menuentry ')==3 and addon in cfg and old not in cfg and 'sq2.debug=1' not in cfg
lines=['#!/bin/sh','set -eu',
 'test "$(cat /sys/block/nvme0n1/device/serial | tr -d \' \')" = CYA5N050012105J77',
 'test "$(cat /media/esp/SQ2-SSD-ID.txt)" = SQ2-INTERNAL-512-20261027',
 'blkid /dev/nvme0n1p6 | grep -q B027-0512',
 'test "$(cat /sys/block/loop0/loop/backing_file)" = /dev/nvme0n1p3',
 'test "$(cat /sys/module/suspend/parameters/screen_only)" = Y',
 'boot=/media/esp/boot/sq2-android1', 'efi=/media/esp/EFI/BOOT',
 'check() { test "$(sha256sum "$1" | cut -d \' \' -f 1)" = "$2"; }',
 f'check "$efi/grub.cfg" {old_cfg}',
 'check "$boot/native-closed-vulkan-v8.cpio.gz" 5ba59bd91c14208442a15792dedff236b0f1789c4d8fe10d00ee50f5e0a4953c',
 f'check "$boot/{old}" {digest(out/old)}',
 f'test ! -e "$boot/{addon}"',
 f'cp "$efi/grub.cfg" "$efi/grub-before-edp-v{rev}.cfg"']
for name,dest in [(addon,f'$boot/{addon}'),('grub-edp-test.cfg','$efi/grub.cfg'),('sq2-edp-test.env','$efi/sq2-edp-test.env')]:
 lines += [f'check /tmp/{name} {digest(out/name)}',f'cp /tmp/{name} "{dest}.new"',f'check "{dest}.new" {digest(out/name)}']
lines += [f'gzip -t "$boot/{addon}.new"','sync',f'mv "$boot/{addon}.new" "$boot/{addon}"',
 'mv "$efi/sq2-edp-test.env.new" "$efi/sq2-edp-test.env"',
 'mv "$efi/grub.cfg.new" "$efi/grub.cfg"','sync',
 f'rm -- "$boot/{old}"','sync','echo SQ2_EDP_REVISION_STAGED']
(out/'stage-revision.sh').write_text('\n'.join(lines)+'\n')
print('Prepared revision',rev,'with verified local backup of superseded addon',previous)
