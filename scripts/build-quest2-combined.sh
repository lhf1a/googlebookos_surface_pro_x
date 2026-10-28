#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${SQ2_GRAPHICS_WORK:-/root/.cache/sq2-graphics}
HIDL=${SQ2_QUEST2_WORK:-/root/.cache/sq2-quest2-hidl}
NDK="$WORK/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin"
OUT="$ROOT/out/quest2-gpu/combined-probe"
mkdir -p "$OUT"
"$NDK/aarch64-linux-android35-clang++" -std=c++17 -O2 -Wall -Wextra -Werror \
 -fno-exceptions -fno-rtti -fno-threadsafe-statics -isystem "$HIDL/include" \
 -I"$WORK/src/mesa-26.2.4/src/util/u_gralloc" -DSQ2_COMBINED_HARNESS -Dmain=quest2_composition_main \
 -c "$ROOT/probe/graphics/quest2-composition.cpp" -o "$OUT/composition.o"
"$NDK/aarch64-linux-android35-clang" -std=c11 -O2 -Wall -Wextra -Werror \
 -DSQ2_COMBINED_HARNESS -Dmain=quest2_vulkan_window_main \
 -c "$ROOT/probe/graphics/quest2-vulkan-window.c" -o "$OUT/vulkan.o"
"$NDK/aarch64-linux-android35-clang++" -std=c++17 -O2 -Wall -Wextra -Werror -static-libstdc++ \
 "$ROOT/probe/graphics/quest2-combined.cpp" "$OUT/composition.o" "$OUT/vulkan.o" \
 -lEGL -lGLESv2 -landroid -lmediandk -ldl -Wl,-z,defs -o "$OUT/quest2-combined"
(cd "$OUT"; sha256sum quest2-combined > COMBINED_PROBE_SHA256SUMS)
echo QUEST2_COMBINED_BUILD_OK
