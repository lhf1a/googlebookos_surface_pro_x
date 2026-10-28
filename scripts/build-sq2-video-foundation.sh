#!/usr/bin/env bash
# Compile-only media foundation. No Image, DTB, installation or hardware access.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
K=/root/.cache/sq2-thermal/kernel
O=/root/.cache/sq2-video-foundation/build
DEST="$ROOT/out/video-decode/foundation"
mkdir -p "$O" "$DEST"
cp "$ROOT/configs/sq2-internal-kernel.config" "$O/.config"
export ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
"$K/scripts/config" --file "$O/.config" --module MEDIA_SUPPORT \
  --enable MEDIA_SUPPORT_FILTER --enable MEDIA_PLATFORM_SUPPORT \
  --module VIDEO_DEV --enable V4L_MEM2MEM_DRIVERS \
  --module VIDEO_QCOM_VENUS --module SM_VIDEOCC_8150 \
  --disable MEDIA_SUBDRV_AUTOSELECT --disable MEDIA_CONTROLLER \
  --disable SND_USB_AUDIO_USE_MEDIA_CONTROLLER
make -s -C "$K" O="$O" olddefconfig
grep -qx 'CONFIG_VIDEO_QCOM_VENUS=m' "$O/.config"
grep -qx 'CONFIG_BLK_DEV_NVME=y' "$O/.config"
grep -qx 'CONFIG_FUSE_FS=m' "$O/.config"
cp "$O/.config" "$DEST/kernel.config"
"$K/scripts/diffconfig" "$ROOT/configs/sq2-internal-kernel.config" "$O/.config" > "$DEST/config.diff"
make -s -C "$K" O="$O" -j8 modules_prepare > "$DEST/prepare.log" 2>&1
# Missing original Module.symvers: compile success is not an ABI validation.
make -s -C "$K" O="$O" -j8 M=drivers/media KBUILD_MODPOST_WARN=1 modules > "$DEST/media-build.log" 2>&1
make -s -C "$K" O="$O" -j8 M=drivers/clk/qcom CONFIG_SM_VIDEOCC_8150=m KBUILD_MODPOST_WARN=1 modules > "$DEST/clock-build.log" 2>&1
find "$O/drivers/media" -name '*.ko' -exec sha256sum '{}' + > "$DEST/module-sha256.txt"
sha256sum "$O/drivers/clk/qcom/videocc-sm8150.ko" >> "$DEST/module-sha256.txt"
echo 'COMPILE_ONLY: no SC8180X platform support, firmware boot, ABI or decode validation'
