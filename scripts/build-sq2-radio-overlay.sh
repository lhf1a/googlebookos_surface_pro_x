#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SDK=/root/.cache/sq2-graphics/sdk/android-15
OUT="$ROOT/out/lte-radio-20261103"
SRC="$ROOT/probe/lte/overlay"
mkdir -p "$OUT" "$ROOT/private/sq2-radio"
test "$#" = 2 || { echo 'usage: build-sq2-radio-overlay.sh OPERATOR_NUMERIC PRIVATE_APN_FILE' >&2; exit 2; }
python3 "$ROOT/scripts/prepare-sq2-radio-apn.py" "$1" "$2"
"$SDK/aapt2" compile --dir "$OUT/overlay-res" -o "$OUT/overlay-res.zip"
"$SDK/aapt2" link --manifest "$SRC/AndroidManifest.xml" -I /root/.cache/sq2-graphics/sdk/android-35/android.jar --auto-add-overlay -o "$OUT/overlay-unsigned.apk" "$OUT/overlay-res.zip"
KEY="$ROOT/private/sq2-radio/local.keystore"
if [ ! -f "$KEY" ]; then
    keytool -genkeypair -keystore "$KEY" -storepass android -keypass android -alias sq2-radio -dname 'CN=SQ2 Local Radio Overlay' -keyalg RSA -keysize 2048 -validity 3650
fi
"$SDK/zipalign" -f -P 16 4 "$OUT/overlay-unsigned.apk" "$OUT/overlay-aligned.apk"
"$SDK/apksigner" sign --ks "$KEY" --ks-pass pass:android --key-pass pass:android --out "$OUT/sq2-radio-overlay.apk" "$OUT/overlay-aligned.apk"
"$SDK/apksigner" verify "$OUT/sq2-radio-overlay.apk"
