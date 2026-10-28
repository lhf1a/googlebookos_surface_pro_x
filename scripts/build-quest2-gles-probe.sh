#!/usr/bin/env bash
# Private direct EGL/GLES probe; does not change any system driver.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${SQ2_GRAPHICS_WORK:-/root/.cache/sq2-graphics}
NDK="$WORK/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64"
OUT="$ROOT/out/quest2-gpu/gles-v77"
mkdir -p "$OUT"
"$NDK/bin/aarch64-linux-android35-clang" -shared -fPIC -O2 -Wall -Wextra -Werror \
  -I"$WORK/src/mesa-26.2.4/include" "$ROOT/probe/graphics/quest2-egl-wrapper.c" \
  -ldl -o "$OUT/libquest2-egl-wrapper.so"
"$NDK/bin/aarch64-linux-android35-clang" -O2 -Wall -Wextra -Werror \
  -DSQ2_QUEST2_GLES -I"$WORK/src/mesa-26.2.4/include" \
  "$ROOT/probe/graphics/gles-readback.c" -Wl,-rpath,'$ORIGIN' \
  -lEGL -lGLESv2 -landroid -ldl -o "$OUT/gles-readback"
patchelf --replace-needed libEGL.so libEGL_adreno.so "$OUT/gles-readback"
patchelf --replace-needed libGLESv2.so libGLESv2_adreno.so "$OUT/gles-readback"
patchelf --replace-needed libandroid.so libnativewindow.so "$OUT/gles-readback"
"$NDK/bin/aarch64-linux-android35-clang" -O2 -Wall -Wextra -Werror \
  -DSQ2_QUEST2_GLES -DSQ2_SYSTEM_EGL -I"$WORK/src/mesa-26.2.4/include" \
  "$ROOT/probe/graphics/gles-readback.c" -lEGL -lGLESv2 -landroid -ldl \
  -o "$OUT/gles-readback-system"
"$NDK/bin/aarch64-linux-android35-clang" -O2 -Wall -Wextra -Werror \
  "$ROOT/probe/graphics/quest2-egl-namespace.c" -o "$OUT/egl-private-namespace"
(cd "$OUT"; sha256sum gles-readback-system egl-private-namespace > SYSTEM_PROBE_SHA256SUMS)
"$NDK/bin/aarch64-linux-android35-clang" -shared -fPIC -O2 -Wall -Wextra -Werror \
  "$ROOT/probe/graphics/quest2-loader-trace.c" -ldl -o "$OUT/libquest2-loader-trace.so"
"$NDK/bin/aarch64-linux-android35-clang" -shared -fPIC -O2 -Wall -Wextra -Werror \
  "$ROOT/probe/graphics/quest2-drm-trace.c" -o "$OUT/libquest2-drm-trace.so"
"$NDK/bin/aarch64-linux-android35-clang" -shared -fPIC -O2 -Wall -Wextra -Werror \
  "$ROOT/probe/graphics/quest2-ion-dmaheap.c" -o "$OUT/libquest2-ion-dmaheap.so"
HIDL=${SQ2_QUEST2_WORK:-/root/.cache/sq2-quest2-hidl}
VENDOR=${SQ2_ANDROID_VENDOR_LIB64:-/root/.cache/sq2-probe-build/googlebook/vendor/lib64}
"$NDK/bin/aarch64-linux-android35-clang++" -std=c++17 -O2 -fPIC -shared -fno-rtti \
  -DSQ2_QTI_MAPPER3 -include "$ROOT/probe/graphics/quest2-platform-cxx.h" \
  -nostdlib++ -Wall -Wextra -Werror -Wno-unused-parameter \
  -isystem "$HIDL/generated" -isystem "$HIDL/include" \
  -I"$WORK/src/mesa-26.2.4/src/util/u_gralloc" \
  "$ROOT/probe/graphics/quest2-qti-mapper.cpp" \
  -L"$VENDOR" -Wl,--no-undefined,-z,global \
  -l:vendor.qti.hardware.display.mapper@3.0.so \
  -l:vendor.qti.hardware.display.mapperextensions@1.0.so \
  -l:vendor.qti.hardware.display.mapperextensions@1.1.so \
  -l:android.hardware.graphics.mapper@3.0.so -lhidlbase -lutils -lc++ -llog -ldl \
  -o "$OUT/libqti-mapper3-bridge.so"
(cd "$OUT"; sha256sum gles-readback libqti-mapper3-bridge.so libquest2-ion-dmaheap.so libquest2-egl-wrapper.so > PROBE_SHA256SUMS)
echo QUEST2_GLES_PROBE_BUILD_OK
