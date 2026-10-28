#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${SQ2_GRAPHICS_WORK:-/root/.cache/sq2-graphics}
OUT="$ROOT/out/sensors-mic-camera/app"
SRC="$ROOT/probe/audio/mic-app"
SDK="$WORK/sdk/android-15"
mkdir -p "$OUT/classes" "$OUT/dex" "$ROOT/private/sq2-microphone"
javac --release 8 -encoding UTF-8 -classpath "$WORK/sdk/android-35/android.jar" -d "$OUT/classes" "$SRC/MainActivity.java"
"$SDK/d8" --min-api 26 --lib "$WORK/sdk/android-35/android.jar" --output "$OUT/dex" "$OUT"/classes/org/surface/sq2/mictest/*.class
"$SDK/aapt2" link --manifest "$SRC/AndroidManifest.xml" -I "$WORK/sdk/android-35/android.jar" -o "$OUT/unsigned.apk"
(cd "$OUT/dex"; zip -q "$OUT/unsigned.apk" classes.dex)
KEY="$ROOT/private/sq2-microphone/debug.keystore"
if [ ! -f "$KEY" ]; then
    keytool -genkeypair -keystore "$KEY" -storepass android -keypass android -alias sq2-test -dname 'CN=SQ2 Local Microphone Test' -keyalg RSA -keysize 2048 -validity 3650
fi
"$SDK/zipalign" -f -P 16 4 "$OUT/unsigned.apk" "$OUT/aligned.apk"
"$SDK/apksigner" sign --ks "$KEY" --ks-pass pass:android --key-pass pass:android --out "$OUT/sq2-mic-test.apk" "$OUT/aligned.apk"
"$SDK/apksigner" verify "$OUT/sq2-mic-test.apk"
