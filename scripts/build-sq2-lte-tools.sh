#!/usr/bin/env bash
# Build the manual read-only LTE probe without audio/Android SDK dependencies.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$ROOT/out/lte/bin"
"${CC:-aarch64-linux-gnu-gcc}" -static -O2 -Wall -Wextra -Werror \
    "$ROOT/probe/test2/sq2-qmi-readonly.c" -o "$ROOT/out/lte/bin/sq2-qmi-readonly"
sha256sum "$ROOT/out/lte/bin/sq2-qmi-readonly"
