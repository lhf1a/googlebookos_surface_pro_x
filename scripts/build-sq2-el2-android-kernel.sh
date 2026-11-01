#!/bin/bash
set -Eeuo pipefail
R=$(cd "$(dirname "$0")/.." && pwd)
B=${SQ2_EL2_ANDROID_BUILD:-/root/.cache/sq2-el2-android-20261031}
D=${SQ2_EL2_ANDROID_OUTPUT:-"$R/out/el2-android"}
mkdir -p "$B" "$D"
if [ ! -d "$B/kernel" ]; then cp -a /root/.cache/sq2-ksunext/kernel "$B/kernel"; fi
K=$B/kernel;O=$B/build
mkdir -p "$O"
cp "$R/configs/sq2-internal-kernel.config" "$O/.config"
python3 "$R/scripts/patch-sq2-screen-only.py" "$K"
python3 - "$K" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1])/'drivers/gpu/drm/msm/adreno/adreno_gpu.c'
s=p.read_text();old='if (!np) {\n\t\tzap_available = false;\n\t\treturn -ENODEV;\n\t}'
new='if (!np || !of_device_is_available(np)) {\n\t\tof_node_put(np);\n\t\tzap_available = false;\n\t\treturn -ENODEV;\n\t}'
if new not in s:
 assert s.count(old)==1;p.write_text(s.replace(old,new))
PY
python3 "$R/scripts/patch-sq2-el2-context-limit.py" "$K"
python3 "$R/scripts/patch-sq2-smmu-smr-groups.py" "$K"
python3 "$R/scripts/patch-sq2-audio-sequencing.py" "$K" --machine-only
python3 "$R/scripts/patch-sq2-el2-preboot-kernel.py" "$K"
C=$K/scripts/config
"$C" --file "$O/.config" -e KSU -e KPROBES -e KSU_KPROBES_HOOK
for n in VIRTUALIZATION KVM VSOCKETS VHOST_MENU VHOST_VSOCK VHOST_NET VIRTIO_VSOCKETS; do "$C" --file "$O/.config" -e "$n"; done
# Android tethering installs CT helpers before its guest forwarding rules.
for n in NETFILTER_XT_TARGET_CT NF_CONNTRACK_FTP NF_CONNTRACK_PPTP NF_NAT_FTP NF_NAT_PPTP; do "$C" --file "$O/.config" -e "$n"; done
# Preserve the normal hardware-decoder foundation with this kernel's ABI.
"$C" --file "$O/.config" -m MEDIA_SUPPORT -d MEDIA_SUPPORT_FILTER \
  -e MEDIA_PLATFORM_SUPPORT -d MEDIA_CAMERA_SUPPORT -d MEDIA_ANALOG_TV_SUPPORT \
  -d MEDIA_DIGITAL_TV_SUPPORT -d MEDIA_RADIO_SUPPORT -d MEDIA_SDR_SUPPORT \
  -d MEDIA_TEST_SUPPORT -m VIDEO_DEV -e V4L_MEM2MEM_DRIVERS \
  -m VIDEO_QCOM_VENUS -m SM_VIDEOCC_8150 -d MEDIA_SUBDRV_AUTOSELECT \
  -d MEDIA_CONTROLLER -d SND_USB_AUDIO_USE_MEDIA_CONTROLLER
export ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
export KBUILD_BUILD_USER=sq2-probe KBUILD_BUILD_HOST=builder
export KBUILD_BUILD_TIMESTAMP='Sat Oct 31 00:00:00 UTC 2026'
make -s -C "$K" O="$O" olddefconfig
"$C" --file "$O/.config" -d MEDIA_CONTROLLER -d SND_USB_AUDIO_USE_MEDIA_CONTROLLER \
  -d DVB_CORE -d MEDIA_USB_SUPPORT -d MEDIA_PCI_SUPPORT -d MEDIA_SUBDRV_AUTOSELECT \
  -d MEDIA_TUNER -d VIDEO_I2C
make -s -C "$K" O="$O" olddefconfig
for n in VIDEO_QCOM_VENUS VIDEO_DEV SM_VIDEOCC_8150; do grep -qx "CONFIG_$n=m" "$O/.config"; done
! grep -qx 'CONFIG_SND_USB_AUDIO_USE_MEDIA_CONTROLLER=y' "$O/.config"
for n in KVM VSOCKETS VHOST_VSOCK BLK_DEV_NVME KSU KPROBES KSU_KPROBES_HOOK; do grep -qx "CONFIG_$n=y" "$O/.config"; done
for n in NETFILTER_XT_TARGET_CT NF_CONNTRACK_FTP NF_CONNTRACK_PPTP NF_NAT_FTP NF_NAT_PPTP; do grep -qx "CONFIG_$n=y" "$O/.config"; done
grep -qx 'CONFIG_FUSE_FS=m' "$O/.config"
make -s -C "$K" O="$O" -j16 Image modules
make -s -C "$K" O="$O" M=drivers/thermal/qcom CONFIG_QCOM_TSENS=m modules
mkdir -p "$B/battery"
cp "$R/probe/power/sq2_battery_compat.c" "$B/battery/"
printf 'obj-m += sq2_battery_compat.o\n' > "$B/battery/Makefile"
make -s -C "$K" O="$O" M="$B/battery" modules
cp "$B/battery/sq2_battery_compat.ko" "$D/"
cp "$O/arch/arm64/boot/Image" "$D/Image"
cp "$O/.config" "$D/kernel.config"
cp "$O/Module.symvers" "$D/Module.symvers"
sha256sum "$D/Image"
