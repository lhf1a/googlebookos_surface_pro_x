#!/usr/bin/env bash
# Build only. Installation/rollback are deliberately separate operations.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${SQ2_GRAPHICS_WORK:-/root/.cache/sq2-graphics}
NDK="$WORK/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64"
OUT="$ROOT/out/quest2-gpu/selector"
mkdir -p "$OUT"
"$NDK/bin/aarch64-linux-android35-clang" -std=c11 -O2 -fPIC -shared \
    -fvisibility=hidden -Wall -Wextra -Werror \
    -I"$WORK/src/mesa-26.2.4/include/android_stub" \
    "$ROOT/probe/graphics/quest2-vulkan-selector.c" \
    -Wl,--no-undefined,-soname,libsq2-vulkan-selector.so -ldl -llog \
    -o "$OUT/vulkan-sq2-selector.so"
"$NDK/bin/llvm-readelf" --dyn-syms "$OUT/vulkan-sq2-selector.so" | grep ' HMI$'
sha256sum "$OUT/vulkan-sq2-selector.so"
echo QUEST2_VULKAN_SELECTOR_BUILD_OK
