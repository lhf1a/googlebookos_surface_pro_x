#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=/root/.cache/sq2-probe-build
K="$B/kernel"
O="$B/kernel-android-out"
D="$ROOT/out/audio-sequencing"
mkdir -p "$D"
grep -qx 'CONFIG_BLK_DEV_NVME=y' "$O/.config"
cp "$O/.config" "$D/kernel.config"
python3 "$ROOT/scripts/patch-sq2-audio-sequencing.py" "$K"
export ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
export KBUILD_BUILD_USER=sq2-probe KBUILD_BUILD_HOST=builder
export KBUILD_BUILD_TIMESTAMP='Tue Oct 27 00:00:00 UTC 2026'
# Full modpost needs the sound-core/SoundWire module symbol table.
make -s -C "$K" O="$O" -j16 modules > "$D/build.log" 2>&1
cmp "$D/kernel.config" "$O/.config"
cp "$O/sound/soc/codecs/snd-soc-wsa881x.ko" "$D/"
cp "$O/sound/soc/qcom/snd-soc-sdm845.ko" "$D/"
aarch64-linux-gnu-strip --strip-debug "$D/"*.ko
for module in "$D/"*.ko; do
    modinfo -F vermagic "$module" | grep -q '^6.3.8-sq2-android1 '
done
sha256sum "$D/"*.ko
