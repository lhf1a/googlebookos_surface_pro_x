#!/usr/bin/env bash
# Read-only layout comparison tool, not a mapper/allocator replacement.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${SQ2_GRAPHICS_WORK:-/root/.cache/sq2-graphics}
NDK="$WORK/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64"
HIDL=${SQ2_QUEST2_WORK:-/root/.cache/sq2-quest2-hidl}
OUT="$ROOT/out/quest2-gpu/layout-reference"
mkdir -p "$OUT"
"$NDK/bin/aarch64-linux-android35-clang++" -std=c++17 -O2 -Wall -Wextra -Werror \
  -I"$WORK/src/mesa-26.2.4/src/util/u_gralloc" \
  -isystem "$HIDL/include" \
  "$ROOT/probe/graphics/quest2-layout-reference.cpp" -static-libstdc++ -landroid -ldl \
  -o "$OUT/layout-reference"
patchelf --replace-needed libandroid.so libnativewindow.so "$OUT/layout-reference"
(cd "$OUT"; sha256sum layout-reference > SHA256SUMS)
echo QUEST2_LAYOUT_REFERENCE_BUILD_OK
