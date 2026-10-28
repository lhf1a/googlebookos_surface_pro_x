#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${SQ2_GRAPHICS_WORK:-/root/.cache/sq2-graphics}
NDK="$WORK/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64"
mkdir -p "$ROOT/out/quest2-gpu"
"$NDK/bin/aarch64-linux-android35-clang" -std=c11 -O2 -Wall -Wextra -Werror -fPIC -shared \
  -I"$WORK/src/mesa-26.2.4/include/android_stub" \
  "$ROOT/probe/graphics/quest2-gralloc-trace.c" -ldl \
  -o "$ROOT/out/quest2-gpu/libgralloc-trace.so"
