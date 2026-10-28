#!/usr/bin/env bash
# Standalone system-loader swapchain/ANativeWindow probe; no driver mutation.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${SQ2_GRAPHICS_WORK:-/root/.cache/sq2-graphics}
NDK="$WORK/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64"
OUT="$ROOT/out/quest2-gpu/vulkan-window-probe"
mkdir -p "$OUT"
"$NDK/bin/aarch64-linux-android35-clang" -std=c11 -O2 -Wall -Wextra -Werror \
  "$ROOT/probe/graphics/quest2-vulkan-window.c" -landroid -lmediandk -ldl \
  -o "$OUT/quest2-vulkan-window"
(cd "$OUT"; sha256sum quest2-vulkan-window > VULKAN_WINDOW_SHA256SUMS)
echo "QUEST2_VULKAN_WINDOW_BUILD_OK $OUT"
