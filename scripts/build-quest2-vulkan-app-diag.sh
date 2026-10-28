#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${SQ2_GRAPHICS_WORK:-/root/.cache/sq2-graphics}
NDK="$WORK/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64"
HIDL=${SQ2_QUEST2_WORK:-/root/.cache/sq2-quest2-hidl}
extra=()
revision=1
case "${1:-}" in
  '') ;;
  --process-read) extra=(-DSQ2_VK_PROCESS_READ);revision=2;;
  --pipeline) extra=(-DSQ2_VK_PROCESS_READ -DSQ2_VK_PIPELINE_DIAGNOSTICS);revision=3;;
  *) exit 2;;
esac
OUT="$ROOT/out/quest2-gpu/vulkan-app-diag-v$revision"
test ! -e "$OUT"
SOURCE="$ROOT/out/quest2-gpu/vulkan-only-counter-v1"
(cd "$SOURCE"; sha256sum -c SHA256SUMS)
mkdir "$OUT"
cp "$SOURCE/"*.so "$OUT/"
"$NDK/bin/aarch64-linux-android35-clang" -std=c11 -O2 -fPIC -shared -Wall -Wextra -Werror \
 -DSQ2_VK_APP_DIAGNOSTICS "${extra[@]}" -isystem "$HIDL/generated" -isystem "$HIDL/include" \
 -I"$WORK/src/mesa-26.2.4/include/android_stub" \
 "$ROOT/probe/graphics/quest2-vulkan-wrapper.c" -L"$OUT" \
 -Wl,--no-undefined,-Bsymbolic -lquest2-v77-ahb -ldl -llog -o "$OUT/libquest2-vulkan-wrapper.so"
(cd "$OUT"; sha256sum *.so > SHA256SUMS)
echo VULKAN_APP_DIAGNOSTICS_BUILD_OK
