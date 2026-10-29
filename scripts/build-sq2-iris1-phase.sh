#!/usr/bin/env bash
# Consumer only: never creates/removes the VIDEOCC overlay/provider.
# Bind via driver_override after the existing zero-power probe has completed.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
STAGE=${SQ2_IDENTIFY_STAGE:-/root/.cache/sq2-iris1-phase}
DEST="$ROOT/out/video-decode/iris1-phase"
mkdir "$STAGE"
mkdir -p "$DEST"
cp "$ROOT/probe/video/sq2_iris1_identify.c" "$STAGE/sq2_iris1_phase.c"
printf 'obj-m += sq2_iris1_phase.o\nccflags-y += -DSQ2_IDENTIFY_EXISTING\n' > "$STAGE/Makefile"
make -s -C /root/.cache/sq2-thermal/kernel \
  O=/root/.cache/sq2-video-foundation/build ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
  M="$STAGE" KBUILD_MODPOST_WARN=1 modules > "$DEST/build.log" 2>&1
cp "$STAGE/sq2_iris1_phase.ko" "$DEST/"
(cd "$DEST" && sha256sum sq2_iris1_phase.ko > SHA256SUMS)
echo 'BUILD_ONLY: default zero-power/no-MMIO; stages require a separate bounded collector'
