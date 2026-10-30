#!/usr/bin/env bash
# Ordinary public-API USB-only playback/capture verification. No root in APK.
set -Eeuo pipefail
R=$(cd "$(dirname "$0")/.." && pwd)
D=${1:-"$R/out/usb-audio-app"}
W=${SQ2_GRAPHICS_WORK:-/root/.cache/sq2-graphics}
S="$W/sdk/android-15"
SRC="$R/probe/audio/usb-app"
KEY=${SQ2_USB_AUDIO_KEY:-"$R/private/sq2-video/debug.keystore"}
test -f "$KEY" || { echo 'Set SQ2_USB_AUDIO_KEY to your own local Android test signing keystore.' >&2; exit 1; }
mkdir -p "$D/classes" "$D/dex"
javac --release 8 -encoding UTF-8 -classpath "$W/sdk/android-35/android.jar" -d "$D/classes" "$SRC/MainActivity.java"
"$S/d8" --min-api 26 --lib "$W/sdk/android-35/android.jar" --output "$D/dex" "$D"/classes/org/surface/sq2/usbtest/*.class
"$S/aapt2" link --manifest "$SRC/AndroidManifest.xml" -I "$W/sdk/android-35/android.jar" -o "$D/unsigned.apk"
(cd "$D/dex"; zip -q "$D/unsigned.apk" classes.dex)
"$S/zipalign" -f -P 16 4 "$D/unsigned.apk" "$D/aligned.apk"
"$S/apksigner" sign --ks "$KEY" --ks-pass pass:android --key-pass pass:android --out "$D/sq2-usb-audio-test.apk" "$D/aligned.apk"
"$S/apksigner" verify "$D/sq2-usb-audio-test.apk"
sha256sum "$D/sq2-usb-audio-test.apk"