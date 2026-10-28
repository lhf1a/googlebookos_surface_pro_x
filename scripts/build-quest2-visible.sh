#!/usr/bin/env bash
# Standalone temporary visible probe. Does not alter runners, vendor, or boot images.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${SQ2_GRAPHICS_WORK:-/root/.cache/sq2-graphics}
NDK="$WORK/android-ndk-r28c"
CC="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang"
SDK="$WORK/sdk/android-15"
ANDROID_JAR="$WORK/sdk/android-35/android.jar"
SRC="$ROOT/probe/graphics/quest2-visible"
OUT="$ROOT/out/quest2-gpu/visible"
test -x "$CC"
test -x "$SDK/d8"
test -f "$ANDROID_JAR"
mkdir -p "$OUT/classes" "$OUT/dex"
javac --release 8 -encoding UTF-8 -Xlint:all -Werror -classpath "$ANDROID_JAR" \
    -d "$OUT/classes" "$SRC/Sq2Quest2Visible.java"
# The cached d8 supports DEX min-api through 34; the target framework remains 35+.
"$SDK/d8" --min-api 34 --lib "$ANDROID_JAR" --output "$OUT/dex" \
    "$OUT/classes/Sq2Quest2Visible.class"
jar --create --file "$OUT/quest2-visible.jar" -C "$OUT/dex" classes.dex
"$CC" -std=c11 -O2 -Wall -Wextra -Werror -fPIC -shared \
    "$SRC/quest2-visible.c" -landroid -lEGL -lGLESv2 -ldl -llog \
    -Wl,-z,defs -Wl,-z,max-page-size=16384 -o "$OUT/libquest2-visible.so"
(cd "$OUT"; sha256sum quest2-visible.jar libquest2-visible.so > VISIBLE_SHA256SUMS)
echo "QUEST2_VISIBLE_BUILD_OK $OUT"
echo 'Deploy only JAR, JNI library, and VISIBLE_SHA256SUMS into a temporary Android directory.'
echo 'Run app_process inside the prepared private EGL namespace with the zygote boot classpath.'
echo 'Use an external timeout. On-screen pixels and screenshots are not build-time verification.'
