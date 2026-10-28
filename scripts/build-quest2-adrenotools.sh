#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${SQ2_QUEST2_WORK:-/root/.cache/sq2-quest2-hidl}
GRAPHICS=${SQ2_GRAPHICS_WORK:-/root/.cache/sq2-graphics}
SRC="$ROOT/upstream/libadrenotools"
test "$(git -c safe.directory="$SRC" -C "$SRC" rev-parse HEAD)" = 8fae8ce254dfc1344527e05301e43f37dea2df80
SUB="$SRC/lib/linkernsbypass"
test "$(git -c safe.directory="$SUB" -C "$SUB" rev-parse HEAD)" = aa3975893d83ef1bc84c321ec60c65fbf1287887
cmake -S "$SRC" -B "$WORK/build-adrenotools" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$GRAPHICS/android-ndk-r28c/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_shared \
  -DBUILD_SHARED_LIBS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build "$WORK/build-adrenotools" -j8
OUT="$ROOT/out/quest2-gpu/hooks"
mkdir -p "$OUT"
cp "$WORK/build-adrenotools/libadrenotools.so" "$WORK/build-adrenotools/src/hook/"*.so "$OUT/"
cp "$GRAPHICS/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" "$OUT/"
