#!/bin/bash
set -Eeuo pipefail
R=$(cd "$(dirname "$0")/.." && pwd)
B=${SQ2_EL2_BUILD:-/root/.cache/sq2-el2-kvm-20261031}
mkdir -p "$B" "$R/out/el2-kvm"
if [ ! -d "$B/kernel" ]; then cp -a /root/.cache/sq2-thermal/kernel "$B/kernel"; fi
K=$B/kernel
O=$B/build
python3 "$R/scripts/patch-sq2-screen-only.py" "$K"
export ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
make -s -C "$K" O="$O" tinyconfig
C=$K/scripts/config
for n in SERIAL_AMBA_PL011 SERIAL_AMBA_PL011_CONSOLE 64BIT ARM64 MMU SMP ARM_GIC ARM_GIC_V3 ARM_ARCH_TIMER ARM_PSCI_FW EFI EFI_STUB OF ACPI PRINTK TTY VT VT_CONSOLE HW_CONSOLE FB FB_EFI FRAMEBUFFER_CONSOLE FONTS FONT_8x16 DEVTMPFS DEVTMPFS_MOUNT PROC_FS SYSFS TMPFS BLK_DEV_INITRD RD_GZIP BINFMT_ELF BINFMT_SCRIPT FUTEX POSIX_TIMERS MULTIUSER SIGNALFD EPOLL VIRTUALIZATION KVM PM SUSPEND; do "$C" --file "$O/.config" -e "$n"; done
"$C" --file "$O/.config" --set-val NR_CPUS 8 --set-str LOCALVERSION '-sq2-el2-kvm-test'
make -s -C "$K" O="$O" olddefconfig
for n in KVM EFI_STUB FB_EFI FRAMEBUFFER_CONSOLE DEVTMPFS SUSPEND; do grep -qx "CONFIG_$n=y" "$O/.config"; done
make -s -C "$K" O="$O" -j16 Image
cp "$O/arch/arm64/boot/Image" "$R/out/el2-kvm/Image"
cp "$O/.config" "$R/out/el2-kvm/kernel.config"
sha256sum "$R/out/el2-kvm/Image"
