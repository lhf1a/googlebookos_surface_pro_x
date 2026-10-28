#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${SQ2_GRAPHICS_WORK:-/root/.cache/sq2-graphics}
CC="$WORK/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang"
OUT="$ROOT/out/quest2-gpu/vulkan-device"
mkdir -p "$OUT"
"$CC" -std=c11 -O2 -Wall -Wextra -Werror "$ROOT/probe/graphics/quest2-vulkan-device.c" \
  -ldl -o "$OUT/quest2-vulkan-device"
"$CC" -std=c11 -O2 -Wall -Wextra -Werror "$ROOT/probe/graphics/quest2-vulkan-namespace.c" \
  -o "$OUT/quest2-vulkan-namespace"
(cd "$OUT";sha256sum quest2-vulkan-device quest2-vulkan-namespace > VULKAN_DEVICE_SHA256SUMS)
echo QUEST2_VULKAN_DEVICE_BUILD_OK
