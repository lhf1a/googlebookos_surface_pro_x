#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${SQ2_GRAPHICS_WORK:-/root/.cache/sq2-graphics}
OUT="$ROOT/out/quest2-gpu/gsl-allocation-trace"
mkdir -p "$OUT"
"$WORK/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang" \
  -std=c11 -O2 -fPIC -shared -Wall -Wextra -Werror \
  "$ROOT/probe/graphics/quest2-gsl-allocation-trace.c" -ldl -o "$OUT/libgsl-allocation-trace.so"
(cd "$OUT"; sha256sum libgsl-allocation-trace.so > SHA256SUMS)
