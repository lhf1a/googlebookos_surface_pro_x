#!/bin/bash
# Additional modules only. No Image replacement, forced insmod or disk writes.
set -Eeuo pipefail
R=$(cd "$(dirname "$0")/.." && pwd)
D=${SQ2_USB_NET_OUTPUT:-"$R/out/usb-network-input-20261102/network"}
B=${SQ2_USB_NET_BUILD:-/root/.cache/sq2-usb-network-20261102}
K=${SQ2_USB_NET_SOURCE:-/root/.cache/sq2-thermal/kernel}
CONFIG=${1:?Pass the exact running kernel configuration}
mkdir -p "$B/build" "$D"
O=$B/build
cp "$CONFIG" "$O/.config"
cp "$CONFIG" "$D/baseline.config"
export ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
export KBUILD_BUILD_USER=sq2-probe KBUILD_BUILD_HOST=builder
C=$K/scripts/config
"$C" --file "$O/.config" -e USB_NET_DRIVERS -m USB_USBNET -m USB_RTL8150 -m USB_RTL8152 \
  -m USB_LAN78XX -m USB_IPHETH -m USB_HSO -m USB_CATC -m USB_KAWETH -m USB_PEGASUS
# Every USB Ethernet/WWAN transport offered by this pinned Linux source.
for n in $(sed -n 's/^config \(USB_NET_[A-Z0-9_]*\)$/\1/p' "$K/drivers/net/usb/Kconfig"); do
    "$C" --file "$O/.config" -m "$n"
done
# Niche non-Ethernet WAN/ATM and non-USB PCI/platform adapters are not enabled.
make -s -C "$K" O="$O" olddefconfig
grep -qx '# CONFIG_MODVERSIONS is not set' "$O/.config"
grep -qx 'CONFIG_USB_NET_AX88179_178A=m' "$O/.config"
grep -qx 'CONFIG_USB_NET_CDC_NCM=m' "$O/.config"
make -s -C "$K" O="$O" -j12 modules_prepare
# The original full Module.symvers is not available. Runtime exports are checked
# independently before any module is loaded; modpost warnings are not an ABI test.
make -s -C "$K" O="$O" -j12 M=drivers/net/usb KBUILD_MODPOST_WARN=1 modules
# New USB PHY dependencies live here; never replace an existing core module.
make -s -C "$K" O="$O" -j12 M=drivers/net/phy KBUILD_MODPOST_WARN=1 modules
make -s -C "$K" O="$O" -j12 M=drivers/net/mdio KBUILD_MODPOST_WARN=1 modules
make -s -C "$K" O="$O" -j12 M=drivers/net KBUILD_MODPOST_WARN=1 mii.ko
make -s -C "$K" O="$O" -j12 M=drivers/usb/class KBUILD_MODPOST_WARN=1 modules
# Upstream puts board-info in vmlinux, although it calls back into libphy.
# Bundle it with the new libphy to avoid a circular module dependency. Only a
# private Kbuild copy changes; the pinned source and running Image stay intact.
S=$B/support
mkdir -p "$S"
mkdir -p "$S/phy"
cp -a "$K/drivers/net/phy/." "$S/phy/"
sed -i 's/obj-y[[:space:]]*+= mdio-boardinfo.o/libphy-y += mdio-boardinfo.o/' "$S/phy/Makefile"
make -s -C "$K" O="$O" -j12 M="$S/phy" KBUILD_MODPOST_WARN=1 modules
cp "$K/net/core/selftests.c" "$S/"
printf '#include <linux/module.h>\nMODULE_LICENSE("GPL");\n' > "$S/license.c"
printf 'obj-m += sq2_usb_net_support.o\nsq2_usb_net_support-y := selftests.o license.o\n' > "$S/Makefile"
make -s -C "$K" O="$O" -j12 M="$S" KBUILD_MODPOST_WARN=1 modules
cp "$O/.config" "$D/network.config"
find "$O/drivers/net/usb" "$O/drivers/net/phy" "$O/drivers/net/mdio" "$O/drivers/usb/class" -name '*.ko' -print0 |
while IFS= read -r -d '' f; do
    dest="$D/modules/kernel/${f#"$O/"}"; mkdir -p "$(dirname "$dest")"; cp "$f" "$dest"
done
mkdir -p "$D/modules/kernel/drivers/net"
cp "$O/drivers/net/mii.ko" "$D/modules/kernel/drivers/net/"
cp "$S/sq2_usb_net_support.ko" "$D/modules/kernel/drivers/net/"
cp "$S/phy/libphy.ko" "$D/modules/kernel/drivers/net/phy/"
find "$D/modules" -name '*.ko' -exec sha256sum {} + > "$D/SHA256SUMS"
echo USB_NETWORK_MODULES_BUILT
