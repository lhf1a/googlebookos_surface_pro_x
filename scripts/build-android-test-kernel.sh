#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=/root/.cache/sq2-probe-build
O="$B/kernel-android-out"
mkdir -p "$O" "$ROOT/out/android-test"
if [ ! -e "$O/Makefile" ]; then cp -a "$B/kernel-out/." "$O/"; fi
cp "$ROOT/out/test2/kernel.config" "$O/.config"
export ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
export KBUILD_BUILD_USER=sq2-probe KBUILD_BUILD_HOST=builder
export KBUILD_BUILD_TIMESTAMP='2026-10-26 00:00:00 UTC'
cd "$B/kernel"
python3 "$ROOT/scripts/prepare-memfd-compat.py"
python3 "$ROOT/scripts/patch-sq2-bluetooth.py"
python3 "$ROOT/scripts/patch-sq2-audio.py"
scripts/kconfig/merge_config.sh -m -O "$O" "$O/.config" "$ROOT/configs/android-test.config" "$ROOT/configs/sq2-audio.config"
make O="$O" olddefconfig
for f in NAMESPACES PID_NS ANDROID_BINDER_IPC ANDROID_BINDERFS EROFS_FS SECURITY_SELINUX BLK_DEV_DM DM_VERITY DM_CRYPT EXT4_FS NETFILTER IP_NF_FILTER IP_NF_MANGLE IP_NF_NAT IP6_NF_FILTER IP6_NF_MANGLE NETFILTER_XT_MATCH_BPF NETFILTER_XT_MATCH_OWNER INET_DIAG_DESTROY; do grep -qx "CONFIG_$f=y" "$O/.config"; done
grep -qx '# CONFIG_BLK_DEV_NVME is not set' "$O/.config"
for f in IP_MULTIPLE_TABLES IPV6_MULTIPLE_TABLES NETFILTER_XT_MATCH_U32 MEMFD_CREATE; do grep -qx "CONFIG_$f=y" "$O/.config"; done
for f in SLIM_QCOM_NGD_CTRL QCOM_BAM_DMA QCOM_APR SND_SOC_SDM845 SND_SOC_WCD934X SND_SOC_WSA881X; do grep -qx "CONFIG_$f=m" "$O/.config"; done
make O="$O" -j16 Image modules qcom/sc8180x-surface-pro-x.dtb
cp "$O/arch/arm64/boot/Image" "$O/.config" "$ROOT/out/android-test/"
cp "$O/arch/arm64/boot/dts/qcom/sc8180x-surface-pro-x.dtb" "$ROOT/out/android-test/"
