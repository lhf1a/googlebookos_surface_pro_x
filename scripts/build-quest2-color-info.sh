#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${SQ2_GRAPHICS_WORK:-/root/.cache/sq2-graphics}
HIDL=${SQ2_QUEST2_WORK:-/root/.cache/sq2-quest2-hidl}
NDK="$WORK/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin"
OUT="$ROOT/out/quest2-gpu/color-info"
mkdir -p "$OUT"
"$NDK/aarch64-linux-android35-clang++" -std=c++17 -O2 -Wall -Wextra -Werror -static-libstdc++ \
 -isystem "$HIDL/include" -I"$WORK/src/mesa-26.2.4/src/util/u_gralloc" \
 "$ROOT/probe/graphics/quest2-color-info.cpp" -landroid -ldl -o "$OUT/quest2-color-info"
(cd "$OUT";sha256sum quest2-color-info > COLOR_INFO_SHA256SUMS)
echo QUEST2_COLOR_INFO_BUILD_OK
