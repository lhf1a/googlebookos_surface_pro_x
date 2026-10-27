#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=/root/.cache/sq2-probe-build
K="$B/kernel"
O="$B/kernel-android-out"
OUT="$ROOT/out/power-connectivity-20261027"
mkdir -p "$OUT"
python3 "$ROOT/scripts/patch-sq2-lid.py" "$K/drivers/platform/surface/surface_aggregator_tabletsw.c"
python3 "$ROOT/scripts/patch-sq2-spi-pm.py" "$K/drivers/hid/spi-hid"
"$K/scripts/kconfig/merge_config.sh" -m -O "$O" "$O/.config" "$ROOT/configs/sq2-lid-power.config"
make -s -C "$K" O="$O" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig
make -s -C "$K" O="$O" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j12 modules > "$OUT/lid-power-build.log" 2>&1
for rel in drivers/platform/surface/surface_aggregator_tabletsw.ko drivers/input/keyboard/gpio_keys.ko drivers/input/misc/pm8941-pwrkey.ko drivers/hid/spi-hid/spi-hid.ko; do
    aarch64-linux-gnu-strip --strip-debug -o "$OUT/$(basename "$rel")" "$O/$rel"
done
echo SQ2_LID_POWER_MODULES_BUILT
