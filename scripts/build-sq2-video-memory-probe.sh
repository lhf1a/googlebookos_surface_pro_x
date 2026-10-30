#!/usr/bin/env bash
# Build only; does not connect to a device or load the module.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
STAGE=/root/.cache/sq2-video-memory-probe
mkdir -p "$STAGE" "$ROOT/out/video-decode"
cp "$ROOT/probe/video/sq2_video_memory_probe.c" "$STAGE/"
printf 'obj-m += sq2_video_memory_probe.o\n' > "$STAGE/Makefile"
make -s -C /root/.cache/sq2-thermal/kernel \
  O=/root/.cache/sq2-video-foundation/build ARCH=arm64 \
  CROSS_COMPILE=aarch64-linux-gnu- M="$STAGE" KBUILD_MODPOST_WARN=1 modules
cp "$STAGE/sq2_video_memory_probe.ko" "$ROOT/out/video-decode/"
sha256sum "$ROOT/out/video-decode/sq2_video_memory_probe.ko"
