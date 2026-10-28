#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${SQ2_THERMAL_WORK:-/root/.cache/sq2-thermal}
STAGE="$WORK/dpcd"
mkdir -p "$STAGE" "$ROOT/out/display-refresh"
cmp "$ROOT/configs/sq2-internal-kernel.config" "$WORK/build/.config"
grep -qx '# CONFIG_MODVERSIONS is not set' "$WORK/build/.config"
cp "$ROOT/probe/graphics/sq2_dpcd_read.c" "$STAGE/"
printf 'obj-m += sq2_dpcd_read.o\n' > "$STAGE/Makefile"
# Original Module.symvers is unavailable. Verify vermagic AND every undefined
# symbol against the live kernel exports before loading. No deployment here.
make -s -C "$WORK/kernel" O="$WORK/build" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
    M="$STAGE" KBUILD_MODPOST_WARN=1 modules
aarch64-linux-gnu-nm -u "$STAGE/sq2_dpcd_read.ko"
modinfo "$STAGE/sq2_dpcd_read.ko"
cp "$STAGE/sq2_dpcd_read.ko" "$ROOT/out/display-refresh/"
sha256sum "$ROOT/out/display-refresh/sq2_dpcd_read.ko"
