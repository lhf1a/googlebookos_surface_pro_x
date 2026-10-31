#!/bin/bash
set -Eeuo pipefail
R=$(cd "$(dirname "$0")/.." && pwd)
B=${SQ2_EL2_ANDROID_BUILD:-/root/.cache/sq2-el2-android-20261031}
mkdir -p "$B" "$R/out/el2-android"
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
C=$K/scripts/config
"$C" --file "$O/.config" -d KSU
for n in VIRTUALIZATION KVM VSOCKETS VHOST_MENU VHOST_VSOCK VHOST_NET VIRTIO_VSOCKETS; do "$C" --file "$O/.config" -e "$n"; done
export ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
export KBUILD_BUILD_USER=sq2-probe KBUILD_BUILD_HOST=builder
export KBUILD_BUILD_TIMESTAMP='Sat Oct 31 00:00:00 UTC 2026'
make -s -C "$K" O="$O" olddefconfig
for n in KVM VSOCKETS VHOST_VSOCK BLK_DEV_NVME; do grep -qx "CONFIG_$n=y" "$O/.config"; done
grep -qx 'CONFIG_FUSE_FS=m' "$O/.config"
make -s -C "$K" O="$O" -j16 Image modules
make -s -C "$K" O="$O" M=drivers/thermal/qcom CONFIG_QCOM_TSENS=m modules
mkdir -p "$B/battery"
cp "$R/probe/power/sq2_battery_compat.c" "$B/battery/"
printf 'obj-m += sq2_battery_compat.o\n' > "$B/battery/Makefile"
make -s -C "$K" O="$O" M="$B/battery" modules
cp "$B/battery/sq2_battery_compat.ko" "$R/out/el2-android/"
cp "$O/arch/arm64/boot/Image" "$R/out/el2-android/Image"
cp "$O/.config" "$R/out/el2-android/kernel.config"
cp "$O/Module.symvers" "$R/out/el2-android/Module.symvers"
sha256sum "$R/out/el2-android/Image"
