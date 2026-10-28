#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${SQ2_GRAPHICS_WORK:-/root/.cache/sq2-graphics}
HIDL=${SQ2_QUEST2_WORK:-/root/.cache/sq2-quest2-hidl}
CXX="$WORK/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang++"
OUT="$ROOT/out/quest2-gpu/color-probe"
mkdir -p "$OUT"
for name in quest2-color quest2-color-mesa;do
 defs=();if [[ "$name" == *-mesa ]];then defs=(-DSQ2_COMPOSITION_MESA_BASELINE);fi
 "$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -fno-threadsafe-statics \
 -static-libstdc++ -isystem "$HIDL/include" -I"$WORK/src/mesa-26.2.4/src/util/u_gralloc" \
 "${defs[@]}" "$ROOT/probe/graphics/quest2-color.cpp" -lEGL -lGLESv2 -landroid -ldl -o "$OUT/$name"
done
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -static-libstdc++ -isystem "$HIDL/include" \
 -I"$WORK/src/mesa-26.2.4/src/util/u_gralloc" "$ROOT/probe/graphics/quest2-color-layout-test.cpp" \
 -ldl -o "$OUT/color-layout-test"
(cd "$OUT";sha256sum quest2-color quest2-color-mesa color-layout-test > COLOR_PROBE_SHA256SUMS)
echo QUEST2_COLOR_BUILD_OK
