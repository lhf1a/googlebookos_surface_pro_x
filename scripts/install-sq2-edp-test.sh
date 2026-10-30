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
check "$efi/grub.cfg" edefa4c8b5028c7fe9dcdc31f9d32d1f7427590d0adc735986c573c3273a5248
check /tmp/edp-rates-v3-addon.cpio.gz 46fec01355da5ffabe3bad55d207dfd801c9eaf87e4da0aa1a6eb7624d5a0209
check /tmp/grub-edp-test.cfg d7af23a37187494f68fc04d2541b2466d14a6af74bc612eec66e733af9237f63
check /tmp/sq2-edp-test.env a2c338796ac6d902013a2d3e52c64b32999752b087ba89646aba66578d9dbd3e
gzip -t /tmp/edp-rates-v3-addon.cpio.gz
test ! -e "$boot/edp-rates-v3-addon.cpio.gz"
check "$efi/grub-before-edp-rates.cfg" 819a8162da843e08c6d01fed3f46ad54910d0cdc6ca9f925680c2062cd8172e6
cp /tmp/edp-rates-v3-addon.cpio.gz "$boot/edp-rates-v3-addon.cpio.gz.new"
cp /tmp/sq2-edp-test.env "$efi/sq2-edp-test.env.new"
cp /tmp/grub-edp-test.cfg "$efi/grub.cfg.edp-new"
check "$boot/edp-rates-v3-addon.cpio.gz.new" 46fec01355da5ffabe3bad55d207dfd801c9eaf87e4da0aa1a6eb7624d5a0209
check "$efi/grub.cfg.edp-new" d7af23a37187494f68fc04d2541b2466d14a6af74bc612eec66e733af9237f63
sync
mv "$boot/edp-rates-v3-addon.cpio.gz.new" "$boot/edp-rates-v3-addon.cpio.gz"
mv "$efi/sq2-edp-test.env.new" "$efi/sq2-edp-test.env"
mv "$efi/grub.cfg.edp-new" "$efi/grub.cfg"
sync
echo SQ2_EDP_ONESHOT_STAGED
cat /proc/sys/kernel/random/boot_id
