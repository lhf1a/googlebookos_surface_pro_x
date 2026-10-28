#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="$ROOT/out/quest2-gpu/kmsg"
mkdir -p "$OUT"
aarch64-linux-gnu-gcc -static -std=c11 -O2 -Wall -Wextra -Werror \
 "$ROOT/probe/graphics/quest2-kmsg-collector.c" -o "$OUT/quest2-kmsg-collector"
(cd "$OUT";sha256sum quest2-kmsg-collector > KMSG_SHA256SUMS)
echo QUEST2_KMSG_BUILD_OK
