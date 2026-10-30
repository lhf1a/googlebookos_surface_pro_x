#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${SQ2_GRAPHICS_WORK:-/root/.cache/sq2-graphics}
SDK="$WORK/sdk/android-15"
SRC="$ROOT/probe/video/player-app"
SIZE=${1:-320x240}
PROFILE=${2:-baseline}
FPS=${3:-30}
case "$FPS" in 30|60) ;; *) exit 2;; esac
case "$PROFILE" in baseline|vp8|vp9) BFRAMES=0;; main|high|hevc|hevc-main10) BFRAMES=3;; *) echo 'Unsupported test profile' >&2; exit 2;; esac
case "$SIZE" in 320x240|1280x720|1920x1080|3840x2160) ;; *) echo 'Unsupported test size' >&2; exit 2;; esac
OUT="$ROOT/out/video-decode/player-app"
if [ "$SIZE" != 320x240 ]; then OUT="$OUT-$SIZE"; fi
if [ "$PROFILE" != baseline ]; then OUT="$OUT-$PROFILE"; fi
if [ "$FPS" != 30 ]; then OUT="$OUT-${FPS}fps"; fi
mkdir -p "$OUT/classes" "$OUT/dex" "$OUT/assets" "$ROOT/private/sq2-video"
printf '%s\n' "$FPS" > "$OUT/assets/fps.txt"
if [ "$PROFILE" = vp8 ] || [ "$PROFILE" = vp9 ]; then
 CODEC=libvpx
 if [ "$PROFILE" = vp9 ]; then CODEC=libvpx-vp9; fi
 # Asset name is historical; MediaExtractor sniffs the WebM container.
 ffmpeg -nostdin -v error -f lavfi -i "testsrc2=size=$SIZE:rate=$FPS" -frames:v "$((FPS*10))" -c:v "$CODEC" -deadline good -cpu-used 4 -threads 4 -pix_fmt yuv420p -g "$FPS" -lag-in-frames 0 -auto-alt-ref 0 -b:v 4M -f webm -y "$OUT/assets/clip.mp4"
elif [ "$PROFILE" = hevc ] || [ "$PROFILE" = hevc-main10 ]; then
 HEVC_PROFILE=main
 PIXFMT=yuv420p
 SOURCE="testsrc2=size=$SIZE:rate=$FPS"
 if [ "$PROFILE" = hevc-main10 ]; then
  HEVC_PROFILE=main10
  PIXFMT=yuv420p10le
  SOURCE+=",format=yuv420p10le,geq=lum='mod(X*7+Y*3+N*19,1024)':cb='mod(X*5+Y*11+N*13,1024)':cr='mod(X*13+Y*7+N*17,1024)'"
 fi
 ffmpeg -nostdin -v error -f lavfi -i "$SOURCE" -frames:v "$((FPS*10))" -c:v libx265 -preset fast -profile:v "$HEVC_PROFILE" -pix_fmt "$PIXFMT" -g "$FPS" -bf 3 -x265-params 'pools=4:frame-threads=2' -tag:v hvc1 -movflags +faststart -y "$OUT/assets/clip.mp4"
else
 ffmpeg -nostdin -v error -f lavfi -i "testsrc2=size=$SIZE:rate=$FPS" -frames:v "$((FPS*10))" -c:v libx264 -profile:v "$PROFILE" -pix_fmt yuv420p -g "$FPS" -bf "$BFRAMES" -movflags +faststart -y "$OUT/assets/clip.mp4"
fi
ffprobe -v error -select_streams v:0 -show_entries stream=profile,level,width,height,has_b_frames,pix_fmt -show_entries frame=pict_type,pts_time -of json "$OUT/assets/clip.mp4" > "$OUT/vector.json"
javac --release 8 -encoding UTF-8 -classpath "$WORK/sdk/android-35/android.jar" -d "$OUT/classes" "$SRC/MainActivity.java"
"$SDK/d8" --min-api 26 --lib "$WORK/sdk/android-35/android.jar" --output "$OUT/dex" "$OUT"/classes/org/surface/sq2/videotest/*.class
"$SDK/aapt2" link --manifest "$SRC/AndroidManifest.xml" -I "$WORK/sdk/android-35/android.jar" -A "$OUT/assets" -0 mp4 -o "$OUT/unsigned.apk"
(cd "$OUT/dex"; zip -q "$OUT/unsigned.apk" classes.dex)
KEY="$ROOT/private/sq2-video/debug.keystore"
if [ ! -f "$KEY" ]; then
 keytool -genkeypair -keystore "$KEY" -storepass android -keypass android -alias sq2-video -dname 'CN=SQ2 Local Video Test' -keyalg RSA -keysize 2048 -validity 3650
fi
"$SDK/zipalign" -f -P 16 4 "$OUT/unsigned.apk" "$OUT/aligned.apk"
"$SDK/apksigner" sign --ks "$KEY" --ks-pass pass:android --key-pass pass:android --out "$OUT/sq2-video-test.apk" "$OUT/aligned.apk"
"$SDK/apksigner" verify "$OUT/sq2-video-test.apk"
