#!/usr/bin/env bash
# Build-only: no device connection, NV, credentials or firmware inputs.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SDK=${SQ2_ANDROID_SDK:-/root/.cache/sq2-graphics/sdk}
OUT=${SQ2_LTE_OUT:-$ROOT/out/lte-data}
mkdir -p "$OUT"/{classes,stubs,dex}
for name in qmi-data qmi-mode rmnet-create; do
    gcc -O2 -Wall -Wextra -Werror -Wno-misleading-indentation "$ROOT/probe/lte/sq2-$name.c" -o "$OUT/test-$name"
    "$OUT/test-$name" selftest
    aarch64-linux-gnu-gcc -O2 -static -Wall -Wextra -Werror -Wno-misleading-indentation "$ROOT/probe/lte/sq2-$name.c" -o "$OUT/sq2-$name"
    qemu-aarch64 "$OUT/sq2-$name" selftest
done
javac --release 8 -cp "$SDK/android-35/android.jar" -d "$OUT/stubs" "$ROOT"/probe/lte/stubs/android/net/*.java
javac --release 8 -cp "$OUT/stubs:$SDK/android-35/android.jar" -d "$OUT/classes" "$ROOT/probe/lte/Sq2Lte.java"
"$SDK/android-15/d8" --min-api 34 --lib "$SDK/android-35/android.jar" --classpath "$OUT/stubs" --output "$OUT/dex" "$OUT"/classes/*.class
jar cf "$OUT/sq2-lte.jar" -C "$OUT/dex" classes.dex
(cd "$OUT";sha256sum sq2-qmi-data sq2-qmi-mode sq2-rmnet-create sq2-lte.jar > SHA256SUMS)
