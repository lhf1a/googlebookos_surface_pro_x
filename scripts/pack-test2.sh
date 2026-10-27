#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=/root/.cache/sq2-probe-build
STAGE=$(mktemp -d "$B/test2-initramfs.XXXXXX")
mkdir -p "$STAGE"/{bin,sbin,proc,sys,dev,run,tmp,logs,media,etc}
cp "$B/busybox/busybox" "$STAGE/bin/busybox"
cp "$ROOT/probe/collect-probe" "$STAGE/bin/collect-base"
cp "$ROOT/probe/test2/init" "$STAGE/init"
for p in collect-probe save-probe run-tests test-input test-power test-gpu test-wifi wifi-services touch-services summarize-tests dhcp-hook network-up start-ssh; do cp "$ROOT/probe/test2/$p" "$STAGE/bin/"; done
cp "$ROOT/out/test2/ssh/"* "$STAGE/bin/"
mkdir -p "$STAGE/etc/dropbear" "$STAGE/root/.ssh"
cp "$ROOT/private/sq2-ssh/"{dropbear_ed25519_host_key,host-public.txt} "$STAGE/etc/dropbear/"
cp "$ROOT/private/sq2-ssh/authorized_keys" "$STAGE/root/.ssh/authorized_keys"
chmod 700 "$STAGE/root" "$STAGE/root/.ssh"
chmod 600 "$STAGE/root/.ssh/authorized_keys" "$STAGE/etc/dropbear/dropbear_ed25519_host_key"
# Password authentication is compiled out of this server.
printf 'root:x:0:0:SQ2 diagnostics:/root:/bin/sh\n' > "$STAGE/etc/passwd"
printf 'root::0:0:99999:7:::\n' > "$STAGE/etc/shadow"
chmod 600 "$STAGE/etc/shadow"
printf 'root:x:0:\n' > "$STAGE/etc/group"
printf '/bin/sh\n' > "$STAGE/etc/shells"
cp "$ROOT/out/test2/iptsd/"* "$STAGE/bin/"
mkdir -p "$STAGE/usr/share/iptsd" "$STAGE/etc/iptsd.d"
cp "$B/iptsd-3.1.1/etc/presets/"*.conf "$STAGE/usr/share/iptsd/"
cp "$ROOT/configs/iptsd-sq2.conf" "$STAGE/etc/iptsd.conf"
for p in refresh-iptsd-input-nodes collect-ghost-touch trial-touch-threshold; do cp "$ROOT/probe/test2/$p" "$STAGE/bin/"; done
cp "$ROOT/out/test2/iw" "$STAGE/bin/"
aarch64-linux-gnu-gcc -static -O2 -Wall -Wextra -Werror -o "$STAGE/bin/input-events" "$ROOT/probe/test2/input-events.c"
aarch64-linux-gnu-gcc -static -O2 -Wall -Wextra -Werror -o "$STAGE/bin/uinput-fixture" "$ROOT/probe/test2/uinput-fixture.c"
# This kernel UAPI uses __user annotations, normally removed by headers_install.
aarch64-linux-gnu-gcc -static -O2 -Wall -Wextra -Werror -D__user= -I"$B/kernel/include/uapi/drm" -o "$STAGE/bin/gpu-probe" "$ROOT/probe/test2/gpu-probe.c"
aarch64-linux-gnu-strip "$STAGE/bin/input-events" "$STAGE/bin/gpu-probe"
chmod 755 "$STAGE/init" "$STAGE/bin/"*
make -C "$B/kernel" O="$B/kernel-out" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- INSTALL_MOD_PATH="$STAGE" INSTALL_MOD_STRIP=1 modules_install
find "$STAGE/lib/modules" -type l -delete
# Include just GPU and Wi-Fi runtime firmware; full DSP/modem resources stay on USB.
mkdir -p "$STAGE/lib/firmware/qcom/msft/surface/pro-x-sq2" "$STAGE/lib/firmware/ath10k/WCN3990/hw1.0"
cp "$ROOT/out/test2/firmware/qcom/"{a680_gmu.bin,a680_sqe.fw} "$STAGE/lib/firmware/qcom/"
cp "$ROOT/out/test2/firmware/qcom/msft/surface/pro-x-sq2/"{qcdxkmsuc8180.mbn,wlanmdsp.mbn} "$STAGE/lib/firmware/qcom/msft/surface/pro-x-sq2/"
cp "$ROOT/out/test2/firmware/ath10k/WCN3990/hw1.0/"{board-2.bin,firmware-5.bin} "$STAGE/lib/firmware/ath10k/WCN3990/hw1.0/"
cp -L /lib/firmware/regulatory.db /lib/firmware/regulatory.db.p7s "$STAGE/lib/firmware/"
python3 "$ROOT/scripts/cpio-list.py" "$STAGE" > "$B/test2-initramfs.list"
"$B/kernel-out/usr/gen_init_cpio" -t 1792972800 "$B/test2-initramfs.list" | gzip -n -9 > "$ROOT/out/test2/initramfs.cpio.gz"
echo "$STAGE" > "$ROOT/out/test2/stage-path.txt"
cp "$STAGE/bin/input-events" "$STAGE/bin/gpu-probe" "$ROOT/out/test2/"
qemu-aarch64 "$STAGE/bin/iw" --version
ls -lh "$ROOT/out/test2/initramfs.cpio.gz"
# A separate addon keeps the large modem/DSP firmware out of input/GPU boots.
WIFI=$(mktemp -d "$B/test2-wifi.XXXXXX")
mkdir -p "$WIFI"/{bin,lib/firmware,etc,dev}
cp -a "$ROOT/out/test2/firmware/qcom" "$WIFI/lib/firmware/"
for f in qrtr-ns pd-mapper tqftpserv rmtfs wpa_supplicant wpa_cli; do cp "$ROOT/out/test2/services/$f" "$WIFI/bin/"; done
cp "$ROOT/out/test2/services/lib/"* "$WIFI/lib/"
cp "$ROOT/private/test2-wpa_supplicant.conf" "$WIFI/etc/wpa_supplicant.conf"
chmod 600 "$WIFI/etc/wpa_supplicant.conf"
chmod 755 "$WIFI/bin/"* "$WIFI/lib/"*.so*
python3 "$ROOT/scripts/cpio-list.py" "$WIFI" > "$B/test2-wifi.list"
"$B/kernel-out/usr/gen_init_cpio" -t 1792972800 "$B/test2-wifi.list" | gzip -n -6 > "$ROOT/out/test2/wifi-addon.cpio.gz"
ls -lh "$ROOT/out/test2/wifi-addon.cpio.gz"
