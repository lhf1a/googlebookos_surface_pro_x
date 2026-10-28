#!/usr/bin/env bash
# Private Vulkan-only adapters. Does not replace the existing GLES mapper.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${SQ2_GRAPHICS_WORK:-/root/.cache/sq2-graphics}
NDK="$WORK/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64"
HIDL=${SQ2_QUEST2_WORK:-/root/.cache/sq2-quest2-hidl}
VENDOR=${SQ2_ANDROID_VENDOR_LIB64:-/root/.cache/sq2-probe-build/googlebook/vendor/lib64}
OUT="$ROOT/out/quest2-gpu/v77-ahb-adapters"
mkdir -p "$OUT"
CXX="$NDK/bin/aarch64-linux-android35-clang++"
INCLUDES=(-isystem "$HIDL/generated" -isystem "$HIDL/include"
  -I"$WORK/src/mesa-26.2.4/include/android_stub"
  -I"$WORK/src/mesa-26.2.4/src/util/u_gralloc")
"$CXX" -std=c++17 -O2 -fPIC -shared -fno-rtti -nostdlib++ -Wall -Wextra -Werror \
  "${INCLUDES[@]}" "$ROOT/probe/graphics/quest2-v77-ahb.cpp" \
  -L"$VENDOR" -Wl,--no-undefined,-z,global -lc++ -ldl -lhardware -landroid \
  -o "$OUT/libquest2-v77-ahb.so"
patchelf --replace-needed libandroid.so libnativewindow.so "$OUT/libquest2-v77-ahb.so"
"$CXX" -std=c++17 -O2 -fPIC -shared -fno-rtti -nostdlib++ -Wall -Wextra -Werror \
  -Wno-unused-parameter -DSQ2_QTI_MAPPER3 -DSQ2_V77_AHB_VIEW \
  -include "$ROOT/probe/graphics/quest2-platform-cxx.h" "${INCLUDES[@]}" \
  "$ROOT/probe/graphics/quest2-qti-mapper.cpp" -L"$VENDOR" -L"$OUT" \
  -Wl,--no-undefined,-z,global -lquest2-v77-ahb \
  -l:vendor.qti.hardware.display.mapper@3.0.so \
  -l:vendor.qti.hardware.display.mapperextensions@1.0.so \
  -l:vendor.qti.hardware.display.mapperextensions@1.1.so \
  -l:android.hardware.graphics.mapper@3.0.so -lhidlbase -lutils -lc++ -llog -ldl \
  -o "$OUT/libqti-mapper3-bridge.so"
"$NDK/bin/aarch64-linux-android35-clang" -std=c11 -O2 -fPIC -shared -Wall -Wextra -Werror \
  "${INCLUDES[@]}" "$ROOT/probe/graphics/quest2-vulkan-wrapper.c" \
  -L"$OUT" -Wl,--no-undefined,-Bsymbolic -lquest2-v77-ahb -ldl \
  -o "$OUT/libquest2-vulkan-wrapper.so"
cp "$ROOT/out/quest2-gpu/gles-v77/libquest2-ion-dmaheap.so" "$OUT/"
(cd "$OUT";sha256sum libqti-mapper3-bridge.so libquest2-ion-dmaheap.so \
  libquest2-v77-ahb.so libquest2-vulkan-wrapper.so > PROBE_SHA256SUMS)
echo QUEST2_V77_AHB_ADAPTERS_BUILD_OK
