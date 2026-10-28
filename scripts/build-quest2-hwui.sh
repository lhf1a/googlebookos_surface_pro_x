#!/usr/bin/env bash
# Actual HWUI/RenderNode producer with ImageReader CPU oracle, no device writes.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${SQ2_GRAPHICS_WORK:-/root/.cache/sq2-graphics}
SDK="$WORK/sdk/android-15"
ANDROID_JAR="$WORK/sdk/android-35/android.jar"
SRC="$ROOT/probe/graphics/quest2-hwui"
OUT="$ROOT/out/quest2-gpu/hwui"
test -x "$SDK/d8"
test -f "$ANDROID_JAR"
mkdir -p "$OUT/classes" "$OUT/dex"
javac --release 8 -encoding UTF-8 -Xlint:all -Werror -classpath "$ANDROID_JAR" \
    -d "$OUT/classes" "$SRC/Sq2Quest2Hwui.java"
"$SDK/d8" --min-api 34 --lib "$ANDROID_JAR" --output "$OUT/dex" \
    "$OUT/classes/Sq2Quest2Hwui.class"
jar --create --file "$OUT/quest2-hwui.jar" -C "$OUT/dex" classes.dex
(cd "$OUT"; sha256sum quest2-hwui.jar > HWUI_SHA256SUMS)
echo "QUEST2_HWUI_BUILD_OK $OUT"
echo 'Launch app_process with zygote classpaths inside private candidate namespace; retain external timeout.'
echo 'SQ2_HWUI_OUTPUT=/data/local/tmp/sq2-closed/hwui-evidence/FRESH_NAME; SQ2_HWUI_MESA=1 for explicit Mesa baseline.'
