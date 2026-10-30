#!/usr/bin/env bash
# Build/copy-only diagnostic; never connects, loads modules or boots firmware.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
K=${SQ2_VIDEO_KERNEL:-/root/.cache/sq2-thermal/kernel}
O=${SQ2_VIDEO_BUILD:-/root/.cache/sq2-video-foundation/build}
STAGE=/root/.cache/sq2-video-firmware-stage
DEST="$ROOT/out/video-decode/firmware-stage"
mkdir -p "$STAGE" "$DEST"
# Pin the audited loader implementation as well as the firmware in the module.
echo "5dd1d028daafe8006400ca50bc6b7ec54a933521f0f6f1c672445e92a9c3d30b  $K/drivers/soc/qcom/mdt_loader.c" | sha256sum -c -
gcc -std=c11 -Wall -Wextra -Werror -O2 \
  "$ROOT/tests/sq2_video_fw_layout_test.c" -o "$STAGE/layout-test"
"$STAGE/layout-test" | tee "$DEST/layout-test.txt"
cp "$ROOT/probe/video/sq2_video_firmware_stage.c" \
   "$ROOT/probe/video/sq2_video_fw_layout.h" "$STAGE/"
printf 'obj-m += sq2_video_firmware_stage.o\n' > "$STAGE/Makefile"
make -s -C "$K" O="$O" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
  M="$STAGE" KBUILD_MODPOST_WARN=1 modules > "$DEST/build.log" 2>&1
cp "$STAGE/sq2_video_firmware_stage.ko" "$DEST/"
aarch64-linux-gnu-nm -u "$DEST/sq2_video_firmware_stage.ko" > "$DEST/undefined.txt"
# Require the copy-only loader; reject accidental direct authentication/start.
grep -q ' U qcom_mdt_load_no_init$' "$DEST/undefined.txt"
if grep -Eq ' U (qcom_scm_|qcom_mdt_load$|qcom_mdt_pas_init$)' "$DEST/undefined.txt"; then
  echo 'Refusing firmware-start/SCM dependency' >&2
  exit 1
fi
modinfo -F vermagic "$DEST/sq2_video_firmware_stage.ko" > "$DEST/vermagic.txt"
(cd "$DEST" && sha256sum sq2_video_firmware_stage.ko > SHA256SUMS)
echo 'BUILD_ONLY: copy/BSS readback probe; ABI and device results require separate validation'
