#!/usr/bin/env bash
# Build an incremental member; installation verifies the existing image first.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=/root/.cache/sq2-probe-build
S=$(mktemp -d "$B/input-posture-amend.XXXXXX")
mkdir -p "$S/bin"
cp "$ROOT/probe/test2/lab-input-posture" "$S/bin/"
cp "$ROOT/probe/test2/init-googlebook-native" "$S/init"
chmod 755 "$S/init" "$S/bin/lab-input-posture"
sh -n "$S/init" "$S/bin/lab-input-posture"
python3 "$ROOT/scripts/cpio-list.py" "$S" > "$S.cpio-list"
"$B/kernel-android-out/usr/gen_init_cpio" -t 1792972800 "$S.cpio-list" | gzip -n -6 > "$ROOT/out/android-test/native-input-fixup.cpio.gz"
sha256sum "$ROOT/out/android-test/native-input-fixup.cpio.gz"
