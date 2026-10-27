#!/usr/bin/env bash
# Local lab test APK; signing key and device resources remain ignored/private.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${SQ2_GRAPHICS_WORK:-/root/.cache/sq2-graphics}
NDK="$WORK/android-ndk-r28c"
CC="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang"
OUT="$ROOT/out/sq2-turnip/app"
SRC="$ROOT/probe/graphics/vulkan-app"
mkdir -p "$OUT/lib/arm64-v8a" "$ROOT/private/sq2-vulkan"
for shader in vert frag; do
  glslangValidator -V --target-env vulkan1.1 --vn "triangle_$shader" \
    "$SRC/triangle.$shader" -o "$OUT/triangle-$shader.h"
done
"$CC" -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -fPIC -shared \
  -I"$OUT" -I"$NDK/sources/android/native_app_glue" \
  "$SRC/main.c" "$NDK/sources/android/native_app_glue/android_native_app_glue.c" \
  -landroid -llog -lvulkan -Wl,-u,ANativeActivity_onCreate \
  -o "$OUT/lib/arm64-v8a/libsq2vulkan.so"
"$WORK/sdk/android-15/aapt2" link --manifest "$SRC/AndroidManifest.xml" -I "$WORK/sdk/android-35/android.jar" -o "$OUT/unsigned.apk" \
  > "$OUT/aapt.log" 2>&1 || { tail -20 "$OUT/aapt.log"; exit 1; }
(cd "$OUT"; zip -q unsigned.apk lib/arm64-v8a/libsq2vulkan.so)
KEY="$ROOT/private/sq2-vulkan/debug.keystore"
if [ ! -f "$KEY" ]; then
  keytool -genkeypair -keystore "$KEY" -storepass android -keypass android \
    -alias sq2-test -dname 'CN=SQ2 Local Vulkan Test' -keyalg RSA -keysize 2048 -validity 3650
fi
"$WORK/sdk/android-15/zipalign" -f -P 16 4 "$OUT/unsigned.apk" "$OUT/aligned.apk"
"$WORK/sdk/android-15/apksigner" sign --ks "$KEY" --ks-pass pass:android --key-pass pass:android \
  --out "$OUT/sq2-vulkan-test.apk" "$OUT/aligned.apk"
"$WORK/sdk/android-15/apksigner" verify "$OUT/sq2-vulkan-test.apk"
echo SQ2_VULKAN_APP_BUILD_OK
