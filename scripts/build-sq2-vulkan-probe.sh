#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${SQ2_GRAPHICS_WORK:-/root/.cache/sq2-graphics}
NDK="$WORK/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64"
OUT="$ROOT/out/sq2-turnip"
mkdir -p "$OUT/runtime"
glslangValidator -V --target-env vulkan1.1 --vn sq2_compute \
  "$ROOT/probe/graphics/vulkan-compute.comp" -o "$OUT/vulkan-compute.h"
for stage in vert frag; do
  glslangValidator -V --target-env vulkan1.1 --vn "sq2_raster_$stage" \
    "$ROOT/probe/graphics/vulkan-raster.$stage" -o "$OUT/vulkan-raster-$stage.h"
done
"$NDK/bin/aarch64-linux-android35-clang" -std=c11 -O2 -Wall -Wextra -Werror \
  -I"$OUT" -I"$WORK/src/mesa-26.2.4/include/android_stub" \
  "$ROOT/probe/graphics/vulkan-readback.c" -ldl -landroid -o "$OUT/runtime/vulkan-readback"
patchelf --replace-needed libandroid.so libnativewindow.so "$OUT/runtime/vulkan-readback"
echo "SQ2_VULKAN_PROBE_BUILD_OK"
