#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=/root/.cache/sq2-probe-build
K="$B/kernel"
O="$B/kernel-android-out"
DEST="$ROOT/out/screen-only"
mkdir -p "$DEST"
# Preserve the current NVMe/audio/graphics/module ABI configuration.
grep -qx 'CONFIG_BLK_DEV_NVME=y' "$O/.config"
grep -qx 'CONFIG_FUSE_FS=m' "$O/.config"
cp "$O/.config" "$DEST/kernel.config"
python3 "$ROOT/scripts/patch-sq2-screen-only.py" "$K"
export ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
export KBUILD_BUILD_USER=sq2-probe KBUILD_BUILD_HOST=builder
export KBUILD_BUILD_TIMESTAMP='2026-10-27 00:00:00 UTC'
make -s -C "$K" O="$O" -j16 Image > "$DEST/build.log" 2>&1
cmp "$DEST/kernel.config" "$O/.config"
cp "$O/arch/arm64/boot/Image" "$DEST/Image"
sha256sum "$DEST/Image"
