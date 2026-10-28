#!/usr/bin/env bash
# Independent exact readback probe; no donor mutation, deployment, or boot changes.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${SQ2_GRAPHICS_WORK:-/root/.cache/sq2-graphics}
HIDL=${SQ2_QUEST2_WORK:-/root/.cache/sq2-quest2-hidl}
CC="$WORK/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang"
CXX="${CC}++"
OUT="$ROOT/out/quest2-gpu/readpixels"
test -x "$CC"
test -x "$CXX"
mkdir -p "$OUT"
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -fno-threadsafe-statics \
    -isystem "$HIDL/include" \
    -I"$WORK/src/mesa-26.2.4/src/util/u_gralloc" \
    -c "$ROOT/probe/graphics/quest2-readpixels-ahb.cpp" -o "$OUT/quest2-readpixels-ahb.o"
"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$ROOT/probe/graphics/quest2-readpixels.c" "$OUT/quest2-readpixels-ahb.o" -lEGL -lGLESv2 -landroid -ldl \
    -Wl,-z,defs -Wl,-z,max-page-size=16384 -o "$OUT/quest2-readpixels"
"$CC" -std=c11 -O2 -Wall -Wextra -Werror -DSQ2_READPIXELS_MESA_BASELINE \
    "$ROOT/probe/graphics/quest2-readpixels.c" "$OUT/quest2-readpixels-ahb.o" -lEGL -lGLESv2 -landroid -ldl \
    -Wl,-z,defs -Wl,-z,max-page-size=16384 -o "$OUT/quest2-readpixels-mesa"
(cd "$OUT"; sha256sum quest2-readpixels quest2-readpixels-mesa > READPIXELS_SHA256SUMS)
echo "QUEST2_READPIXELS_BUILD_OK $OUT"
echo 'Run only via the existing private system EGL namespace with an external timeout.'
echo 'Optional arguments: --size WIDTH HEIGHT --path client|pbo|both --no-errors'
echo 'Separate exact texture relay mode: --relay-to-ahb|--upload-relay [--size WIDTH HEIGHT]'
echo 'Build success does not establish hardware pixel or error-case correctness.'
