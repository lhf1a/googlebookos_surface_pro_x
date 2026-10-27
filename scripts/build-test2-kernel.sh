#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=/root/.cache/sq2-probe-build
export ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
export KBUILD_BUILD_USER=sq2-probe KBUILD_BUILD_HOST=builder
export KBUILD_BUILD_TIMESTAMP='2026-10-26 00:00:00 UTC'
mkdir -p "$ROOT/out/test2"
python3 "$ROOT/scripts/patch-test3-kernel.py"
cp "$ROOT/upstream/aarch64-kernel-configs/full/spx-5.19.0-small.config" "$BUILD/kernel-out/.config"
cd "$BUILD/kernel"
scripts/kconfig/merge_config.sh -m -O "$BUILD/kernel-out" "$BUILD/kernel-out/.config" "$ROOT/configs/diagnostic.config" "$ROOT/configs/test2.config"
make O="$BUILD/kernel-out" olddefconfig
grep -qx 'CONFIG_INPUT_MISC=y' "$BUILD/kernel-out/.config"
grep -qx 'CONFIG_INPUT_UINPUT=y' "$BUILD/kernel-out/.config"
grep -qx 'CONFIG_UNIX98_PTYS=y' "$BUILD/kernel-out/.config"
make O="$BUILD/kernel-out" -j16 Image modules qcom/sc8180x-surface-pro-x.dtb
cp "$BUILD/kernel-out/arch/arm64/boot/Image" "$ROOT/out/test2/"
cp "$BUILD/kernel-out/arch/arm64/boot/dts/qcom/sc8180x-surface-pro-x.dtb" "$ROOT/out/test2/"
cp "$BUILD/kernel-out/.config" "$ROOT/out/test2/kernel.config"
