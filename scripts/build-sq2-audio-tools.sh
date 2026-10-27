#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
T=/root/.cache/sq2-probe-build/tinyalsa
mkdir -p "$ROOT/out/audio-lte/bin"
make -C "$T/src" -j4 CROSS_COMPILE=aarch64-linux-gnu- libtinyalsa.a
for tool in tinymix tinyplay tinypcminfo; do
    aarch64-linux-gnu-gcc -static -O2 -I"$T/include" "$T/utils/$tool.c" "$T/src/libtinyalsa.a" -lm -ldl -o "$ROOT/out/audio-lte/bin/$tool"
done
aarch64-linux-gnu-gcc -static -O2 -Wall -Wextra -Werror "$ROOT/probe/test2/sq2-qmi-readonly.c" -o "$ROOT/out/audio-lte/bin/sq2-qmi-readonly"
git -C "$T" rev-parse HEAD > "$ROOT/out/audio-lte/tinyalsa-commit.txt"
