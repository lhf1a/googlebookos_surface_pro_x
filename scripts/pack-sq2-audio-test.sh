#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=/root/.cache/sq2-probe-build
S=$(mktemp -d "$B/audio-test.XXXXXX")
make -s -C "$B/kernel" O="$B/kernel-android-out" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- INSTALL_MOD_PATH="$S" INSTALL_MOD_STRIP=1 modules_install
find "$S/lib/modules" -type l -delete
mkdir -p "$S/bin"
cp "$ROOT/out/audio-lte/bin/"* "$S/bin/"
cp "$ROOT/probe/test2/"{audio-drivers-start,test-speaker-readiness} "$S/bin/"
chmod 755 "$S/bin/"*
python3 "$ROOT/scripts/cpio-list.py" "$S" > "$S.list"
"$B/kernel-android-out/usr/gen_init_cpio" -t 1792972800 "$S.list" | gzip -n -6 > "$ROOT/out/audio-lte/audio-addon.cpio.gz"
cp "$ROOT/out/android-test/Image" "$ROOT/out/audio-lte/Image"
cp "$ROOT/out/android-test/sc8180x-surface-pro-x.dtb" "$ROOT/out/audio-lte/"
cp "$ROOT/out/android-test/.config" "$ROOT/out/audio-lte/kernel.config"
sha256sum "$ROOT/out/audio-lte/"{Image,sc8180x-surface-pro-x.dtb,audio-addon.cpio.gz}
