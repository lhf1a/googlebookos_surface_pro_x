#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="$ROOT/out/connectivity"
CLANG="${SQ2_ANDROID_CC:-}"
if [[ -z "$CLANG" ]]; then
    for cand in \
        /mnt/spx-workspace/toolchains/sq2-graphics/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang \
        /root/.cache/sq2-graphics/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang; do
        if [[ -x "$cand" ]]; then
            CLANG="$cand"
            break
        fi
    done
fi
if [[ -z "$CLANG" || ! -x "$CLANG" ]]; then
    echo "missing aarch64-linux-android35-clang (set SQ2_ANDROID_CC)" >&2
    exit 1
fi
mkdir -p "$OUT"
aarch64-linux-gnu-gcc -static -O2 -Wall -Wextra -Werror "$ROOT/probe/test2/sq2-hci-bridge.c" -o "$OUT/sq2-hci-bridge"
"$CLANG" -shared -fPIC -O2 -Wall -Wextra -Werror "$ROOT/probe/test2/sq2-wifi-driver.c" -llog -o "$OUT/libsq2-wifi-driver.so"
"$CLANG" -c -fPIC -O2 -Wall -Wextra -Werror "$ROOT/probe/test2/sq2-wifi-driver.c" -o "$OUT/wifi-driver.o"
"${CLANG}++" -shared -fPIC -O2 -std=c++17 -Wall -Wextra -Werror -static-libstdc++ \
    -I"$OUT/source/headers" "$ROOT/probe/test2/sq2-wifi-hal.cpp" "$OUT/wifi-driver.o" -llog \
    -o "$OUT/libsq2-wifi-hal.so"
echo 'SQ2 connectivity adapters built; hardware integration is tested separately.'
