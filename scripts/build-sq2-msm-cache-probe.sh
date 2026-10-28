#!/usr/bin/env bash
# Checks allocation support only; run Vulkan CPU/GPU coherence checks separately.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${SQ2_GRAPHICS_WORK:-/root/.cache/sq2-graphics}
OUT="$ROOT/out/quest2-gpu/msm-cache-probe"
mkdir -p "$OUT"
"$WORK/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang" \
  -std=c11 -D_GNU_SOURCE -O2 -Wall -Wextra -Werror \
  "$ROOT/probe/graphics/msm-cache-modes.c" -o "$OUT/msm-cache-modes"
(cd "$OUT"; sha256sum msm-cache-modes > SHA256SUMS)
echo MSM_CACHE_PROBE_BUILD_OK
