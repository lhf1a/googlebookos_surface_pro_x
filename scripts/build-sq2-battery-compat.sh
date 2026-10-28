#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${SQ2_THERMAL_WORK:-/root/.cache/sq2-thermal}
STAGE="$WORK/battery-compat"
mkdir -p "$STAGE" "$ROOT/out/resource-monitor"
cmp "$ROOT/configs/sq2-internal-kernel.config" "$WORK/build/.config"
grep -qx '# CONFIG_MODVERSIONS is not set' "$WORK/build/.config"
cp "$ROOT/probe/power/sq2_battery_compat.c" "$STAGE/"
printf 'obj-m += sq2_battery_compat.o\n' > "$STAGE/Makefile"
# Prepared matching source/config as documented for build-sq2-tsens.sh.
# Original Module.symvers unavailable: verify every runtime export before load.
make -s -C "$WORK/kernel" O="$WORK/build" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
    M="$STAGE" KBUILD_MODPOST_WARN=1 modules
cp "$STAGE/sq2_battery_compat.ko" "$ROOT/out/resource-monitor/"
sha256sum "$ROOT/out/resource-monitor/sq2_battery_compat.ko"
