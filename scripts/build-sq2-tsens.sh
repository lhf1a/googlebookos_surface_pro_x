#!/usr/bin/env bash
# Matching kernel module only; no replacement Image or unrelated modules.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${SQ2_THERMAL_WORK:-/root/.cache/sq2-thermal}
DEST="$ROOT/out/gpu-opp"
mkdir -p "$WORK/kernel" "$WORK/build" "$DEST"
echo "275f05c5608c5906a1fe43dbc001a63c06296bee54e6fd29cb2ff440b488cdb5  $ROOT/downloads/surface-kernel-b86f786.tar.gz" | sha256sum -c -
if [ ! -f "$WORK/kernel/Makefile" ]; then
    tar -xf "$ROOT/downloads/surface-kernel-b86f786.tar.gz" --strip-components=1 -C "$WORK/kernel"
fi
cp "$ROOT/configs/sq2-internal-kernel.config" "$WORK/build/.config"
export ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
make -s -C "$WORK/kernel" O="$WORK/build" olddefconfig
cmp "$ROOT/configs/sq2-internal-kernel.config" "$WORK/build/.config"
make -s -C "$WORK/kernel" O="$WORK/build" -j8 modules_prepare
# No original Module.symvers cache exists. MODVERSIONS is disabled. This builds
# the module but is NOT a full modpost validation: before insmod, verify every
# undefined symbol against the target's __ksymtab exports and check vermagic.
grep -qx '# CONFIG_MODVERSIONS is not set' "$WORK/build/.config"
make -s -C "$WORK/kernel" O="$WORK/build" -j8 M=drivers/thermal/qcom \
    CONFIG_QCOM_TSENS=m KBUILD_MODPOST_WARN=1 modules
cp "$WORK/build/drivers/thermal/qcom/qcom_tsens.ko" "$DEST/"
sha256sum "$DEST/qcom_tsens.ko"
