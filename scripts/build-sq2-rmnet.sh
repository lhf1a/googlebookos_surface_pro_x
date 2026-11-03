#!/usr/bin/env bash
# Build stock RMNET against an existing, matching fully built kernel tree.
set -Eeuo pipefail
test "$#" = 3 || { echo 'usage: build-sq2-rmnet.sh KERNEL_SOURCE KERNEL_BUILD NEW_OUTPUT' >&2;exit 2; }
SOURCE=$(realpath "$1")
BUILD=$(realpath "$2")
OUT=$(realpath -m "$3")
test ! -e "$OUT"
test -s "$BUILD/Module.symvers"
test -f "$BUILD/.config"
config=$(sha256sum "$BUILD/.config")
mkdir -p "$OUT"
cp -a "$SOURCE/drivers/net/ethernet/qualcomm/rmnet/." "$OUT/"
make -C "$SOURCE" O="$BUILD" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- M="$OUT" CONFIG_RMNET=m modules
test "$(sha256sum "$BUILD/.config")" = "$config"
modinfo "$OUT/rmnet.ko"
sha256sum "$OUT/rmnet.ko"
