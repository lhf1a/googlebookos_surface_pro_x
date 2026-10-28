#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${SQ2_QUEST2_WORK:-/root/.cache/sq2-quest2-hidl}
GRAPHICS=${SQ2_GRAPHICS_WORK:-/root/.cache/sq2-graphics}
NDK="$GRAPHICS/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64"
VENDOR=${SQ2_ANDROID_VENDOR_LIB64:-/root/.cache/sq2-probe-build/googlebook/vendor/lib64}
OUT="$ROOT/out/quest2-gpu"
mkdir -p "$OUT"
includes=(-isystem "$WORK/generated" -isystem "$WORK/include" -I"$GRAPHICS/src/mesa-26.2.4/src/util/u_gralloc")
"$NDK/bin/aarch64-linux-android35-clang++" -std=c++17 -O2 -Wall -Wextra -Werror \
  -static-libstdc++ "${includes[@]}" "$ROOT/probe/graphics/quest2-mapper-info.cpp" \
  -ldl -landroid -o "$OUT/mapper-info"
patchelf --replace-needed libandroid.so libnativewindow.so "$OUT/mapper-info"
"$NDK/bin/aarch64-linux-android35-clang++" -std=c++17 -O2 -fPIC -shared -fno-rtti \
  -include "$ROOT/probe/graphics/quest2-platform-cxx.h" \
  -nostdlib++ -Wall -Wextra -Werror -Wno-unused-parameter \
  "${includes[@]}" "$ROOT/probe/graphics/quest2-qti-mapper.cpp" \
  -L"$VENDOR" -Wl,--no-undefined,-z,global \
  -l:vendor.qti.hardware.display.mapper@4.0.so \
  -l:vendor.qti.hardware.display.mapperextensions@1.0.so \
  -l:vendor.qti.hardware.display.mapperextensions@1.1.so \
  -l:android.hardware.graphics.mapper@4.0.so -lhidlbase -lutils -lc++ -llog -ldl \
  -o "$OUT/libqti-mapper-bridge.so"
echo QTI_MAPPER_EXPERIMENT_BUILD_OK
