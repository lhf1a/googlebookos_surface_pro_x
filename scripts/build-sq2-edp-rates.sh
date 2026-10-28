#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=/root/.cache/sq2-thermal
STAGE=/root/.cache/sq2-edp-rates
test ! -e "$STAGE"
cmp "$ROOT/configs/sq2-internal-kernel.config" "$WORK/build/.config"
mkdir -p "$STAGE/drivers/gpu/drm" "$ROOT/out/display-refresh"
cp -a "$WORK/kernel/drivers/gpu/drm/msm" "$STAGE/drivers/gpu/drm/"
cp "$WORK/kernel/drivers/gpu/drm/"*.h "$STAGE/drivers/gpu/drm/"
python3 "$ROOT/scripts/patch-sq2-edp-rates.py" "$STAGE"
python3 "$ROOT/scripts/patch-sq2-edp-diag.py" "$STAGE"
make -s -C "$WORK/kernel" O="$WORK/build" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
    M="$STAGE/drivers/gpu/drm/msm" KBUILD_MODPOST_WARN=1 -j12 modules \
    > "$ROOT/out/display-refresh/msm-build.log" 2>&1
cp "$STAGE/drivers/gpu/drm/msm/msm.ko" "$ROOT/out/display-refresh/msm-edp-rates.ko"
aarch64-linux-gnu-nm -u "$STAGE/drivers/gpu/drm/msm/msm.ko" > "$ROOT/out/display-refresh/msm-undefined.txt"
python3 "$ROOT/scripts/restore-sq2-msm-depends.py"
modinfo "$ROOT/out/display-refresh/msm-edp-rates.ko"
sha256sum "$ROOT/out/display-refresh/msm-edp-rates.ko"
