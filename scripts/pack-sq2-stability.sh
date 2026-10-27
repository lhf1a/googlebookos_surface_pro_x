#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=/root/.cache/sq2-probe-build
O="$ROOT/out/stability-20261027"
S=$(mktemp -d "$B/stability-addon.XXXXXX")
mkdir -p "$O" "$S/etc/googlebook-security/lib64/egl"
cp "$ROOT/out/sq2-graphics/runtime/libEGL_freedreno.so" "$S/etc/googlebook-security/lib64/egl/"
python3 "$ROOT/scripts/cpio-list.py" "$S" > "$S.list"
"$B/kernel-android-out/usr/gen_init_cpio" -t 1793059200 "$S.list" | gzip -n -6 > "$O/stability-addon.cpio.gz"
sha256sum "$O/stability-addon.cpio.gz" "$S/etc/googlebook-security/lib64/egl/libEGL_freedreno.so" > "$O/stability-addon.sha256"
cat "$O/stability-addon.sha256"
