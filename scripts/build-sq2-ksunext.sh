#!/usr/bin/env bash
# Isolated compile-only candidate. No installation or boot-menu writes.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=/root/.cache/sq2-ksunext
K="$WORK/kernel"
O="$WORK/builtin-build"
DEST="$ROOT/out/ksunext"
mkdir -p "$DEST"
export ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
export KBUILD_BUILD_USER=sq2-probe KBUILD_BUILD_HOST=builder
export KBUILD_BUILD_TIMESTAMP='Sat Oct 31 00:00:00 UTC 2026'
export KBUILD_BUILD_VERSION=2
grep -q SQ2_SCREEN_ONLY_V1 "$K/kernel/power/suspend.c"
grep -qx 'CONFIG_KSU=y' "$O/.config"
grep -qx 'CONFIG_KSU_KPROBES_HOOK=y' "$O/.config"
grep -qx 'CONFIG_BLK_DEV_NVME=y' "$O/.config"
grep -qx 'CONFIG_FUSE_FS=m' "$O/.config"
make -s -C "$K" O="$O" headers_install
make -s -C "$K" O="$O" -j12 Image > "$DEST/build.log" 2>&1
cp "$O/arch/arm64/boot/Image" "$DEST/Image-candidate"
cp "$O/.config" "$DEST/kernel.config"
sha256sum "$DEST/Image-candidate" "$DEST/kernel.config" > "$DEST/SHA256SUMS"
echo 'Compile-only candidate; ABI, QEMU and device boot validation still required.'
