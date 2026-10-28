#!/bin/sh
# Run on the identified lab Surface only after uploading the three /tmp files.
set -eu
test "$(cat /sys/block/nvme0n1/device/serial | tr -d ' ')" = CYA5N050012105J77
test "$(cat /media/esp/SQ2-SSD-ID.txt)" = SQ2-INTERNAL-512-20261027
test -f /media/esp/SQ2-PROBE-MEDIA.txt
blkid /dev/nvme0n1p6 | grep -q 'UUID="B027-0512"'
blkid /dev/nvme0n1p1 | grep -q 'UUID="4f46d2ec-323e-486e-b473-fc2adc0849ba"'
grep -q '^/dev/nvme0n1p6 /media/esp vfat ' /proc/mounts
test "$(readlink -f /dev/mapper/system_a)" = /dev/loop0
test "$(cat /sys/block/loop0/loop/backing_file)" = /dev/nvme0n1p3
test "$(cat /sys/module/suspend/parameters/screen_only)" = Y
boot=/media/esp/boot/sq2-android1
efi=/media/esp/EFI/BOOT
check() { test "$(sha256sum "$1" | cut -d ' ' -f 1)" = "$2"; }
check "$boot/native-closed-vulkan-v8.cpio.gz" 5ba59bd91c14208442a15792dedff236b0f1789c4d8fe10d00ee50f5e0a4953c
check "$efi/grub.cfg" 819a8162da843e08c6d01fed3f46ad54910d0cdc6ca9f925680c2062cd8172e6
check /tmp/edp-rates-v2-addon.cpio.gz c10e9dc952e39e8a96a500f2172c927bdb42c6a29c0257da61752065d34a0153
check /tmp/grub-edp-test.cfg 371822d312b7b482cce30581304c124b19cbd811da68a559cc20d28dbfe3a8c6
check /tmp/sq2-edp-test.env a2c338796ac6d902013a2d3e52c64b32999752b087ba89646aba66578d9dbd3e
gzip -t /tmp/edp-rates-v2-addon.cpio.gz
test ! -e "$boot/edp-rates-v2-addon.cpio.gz"
test ! -e "$efi/grub-before-edp-rates.cfg"
cp "$efi/grub.cfg" "$efi/grub-before-edp-rates.cfg"
cp /tmp/edp-rates-v2-addon.cpio.gz "$boot/edp-rates-v2-addon.cpio.gz.new"
cp /tmp/sq2-edp-test.env "$efi/sq2-edp-test.env.new"
cp /tmp/grub-edp-test.cfg "$efi/grub.cfg.edp-new"
check "$boot/edp-rates-v2-addon.cpio.gz.new" c10e9dc952e39e8a96a500f2172c927bdb42c6a29c0257da61752065d34a0153
check "$efi/grub.cfg.edp-new" 371822d312b7b482cce30581304c124b19cbd811da68a559cc20d28dbfe3a8c6
sync
mv "$boot/edp-rates-v2-addon.cpio.gz.new" "$boot/edp-rates-v2-addon.cpio.gz"
mv "$efi/sq2-edp-test.env.new" "$efi/sq2-edp-test.env"
mv "$efi/grub.cfg.edp-new" "$efi/grub.cfg"
sync
echo SQ2_EDP_ONESHOT_STAGED
cat /proc/sys/kernel/random/boot_id
