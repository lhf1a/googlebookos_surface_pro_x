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
python3 "$ROOT/scripts/patch-sq2-edp-cached-diag.py" "$STAGE"
# The shared compiler fixes external-M include paths and restores original deps.
bash "$ROOT/scripts/compile-sq2-edp-rates.sh"
