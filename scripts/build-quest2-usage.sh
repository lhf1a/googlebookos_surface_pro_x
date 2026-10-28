#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${SQ2_GRAPHICS_WORK:-/root/.cache/sq2-graphics}
HIDL=${SQ2_QUEST2_WORK:-/root/.cache/sq2-quest2-hidl}
CXX="$WORK/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang++"
OUT="$ROOT/out/quest2-gpu/usage-probe"
mkdir -p "$OUT"
CC="$WORK/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang"
"$CC" -std=c11 -O2 -Wall -Wextra -Werror -c "$ROOT/probe/graphics/quest2-upload-trace.c" -o "$OUT/upload-trace.o"
for name in quest2-usage quest2-usage-mesa;do
 defs=();if [[ "$name" == *-mesa ]];then defs=(-DSQ2_COMPOSITION_MESA_BASELINE);fi
 "$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -fno-threadsafe-statics \
 -static-libstdc++ -isystem "$HIDL/include" -I"$WORK/src/mesa-26.2.4/src/util/u_gralloc" \
 "${defs[@]}" "$ROOT/probe/graphics/quest2-usage.cpp" "$OUT/upload-trace.o" -lEGL -lGLESv2 -landroid -ldl -o "$OUT/$name"
done
(cd "$OUT";sha256sum quest2-usage quest2-usage-mesa > USAGE_PROBE_SHA256SUMS)
echo QUEST2_USAGE_BUILD_OK
