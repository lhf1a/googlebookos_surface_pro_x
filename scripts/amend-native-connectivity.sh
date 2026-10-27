#!/usr/bin/env bash
# Append a tiny initramfs member for script-only fixes after a full upload.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=/root/.cache/sq2-probe-build
S=$(mktemp -d "$B/connectivity-amend.XXXXXX")
mkdir -p "$S/bin"
cp "$ROOT/probe/test2/"{wifi-framework-boot,wifi-rollback} "$S/bin/"
chmod 755 "$S/bin/"*
python3 "$ROOT/scripts/cpio-list.py" "$S" > "$S.cpio-list"
"$B/kernel-android-out/usr/gen_init_cpio" -t 1792972800 "$S.cpio-list" | gzip -n -6 > "$ROOT/out/android-test/native-connectivity-fixup.cpio.gz"
cat "$ROOT/out/android-test/native-connectivity-fixup.cpio.gz" >> "$ROOT/out/android-test/native-combined-initramfs.cpio.gz"
echo 'Appended connectivity script update to combined initramfs'
