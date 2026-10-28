#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=/root/.cache/sq2-thermal
STAGE=/root/.cache/sq2-edp-rates
cp "$WORK/kernel/drivers/gpu/drm/"*.h "$STAGE/drivers/gpu/drm/"
python3 - "$STAGE/drivers/gpu/drm/msm/Makefile" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1])
p.write_text(p.read_text().replace('$(srctree)/$(src)', '$(src)'))
PY
make -s -C "$WORK/kernel" O="$WORK/build" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
    M="$STAGE/drivers/gpu/drm/msm" KBUILD_MODPOST_WARN=1 -j12 modules \
    > "$ROOT/out/display-refresh/msm-build.log" 2>&1
cp "$STAGE/drivers/gpu/drm/msm/msm.ko" "$ROOT/out/display-refresh/msm-edp-rates.ko"
aarch64-linux-gnu-nm -u "$STAGE/drivers/gpu/drm/msm/msm.ko" > "$ROOT/out/display-refresh/msm-undefined.txt"
modinfo "$ROOT/out/display-refresh/msm-edp-rates.ko"
sha256sum "$ROOT/out/display-refresh/msm-edp-rates.ko"
