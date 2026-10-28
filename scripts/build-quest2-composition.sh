#!/usr/bin/env bash
# Independent multi-layer/native-fence probe; no deployment or donor mutation.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${SQ2_GRAPHICS_WORK:-/root/.cache/sq2-graphics}
HIDL=${SQ2_QUEST2_WORK:-/root/.cache/sq2-quest2-hidl}
CXX="$WORK/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang++"
OUT="$ROOT/out/quest2-gpu/composition"
test -x "$CXX"
mkdir -p "$OUT"
for variant in quest2-composition quest2-composition-mesa; do
    definitions=()
    if [[ "$variant" == *-mesa ]]; then definitions=(-DSQ2_COMPOSITION_MESA_BASELINE); fi
    "$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -fno-threadsafe-statics \
        -static-libstdc++ -isystem "$HIDL/include" \
        -I"$WORK/src/mesa-26.2.4/src/util/u_gralloc" "${definitions[@]}" \
        "$ROOT/probe/graphics/quest2-composition.cpp" -lEGL -lGLESv2 -landroid -ldl \
        -Wl,-z,defs -Wl,-z,max-page-size=16384 -o "$OUT/$variant"
done
(cd "$OUT"; sha256sum quest2-composition quest2-composition-mesa > COMPOSITION_SHA256SUMS)
echo "QUEST2_COMPOSITION_BUILD_OK $OUT"
echo 'Use the existing private system-EGL namespace and gles-linked-zero-area candidate.'
echo 'SQ2_COMPOSITION_WIDTH/HEIGHT default 64 (1..4096); SQ2_COMPOSITION_CYCLES defaults 3 (1..150).'
echo 'The separate Mesa binary requires the original Android mount namespace.'
echo 'External timeout required. Build success is not device synchronization/pixel validation.'
