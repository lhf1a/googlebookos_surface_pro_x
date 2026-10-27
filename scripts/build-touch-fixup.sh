#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=/root/.cache/sq2-probe-build
S=$(mktemp -d "$B/touch-amend.XXXXXX")
mkdir -p "$S/bin" "$S/etc"
cp "$ROOT/configs/iptsd-sq2.conf" "$S/etc/iptsd.conf"
aarch64-linux-gnu-strip -o "$S/bin/iptsd" "$B/iptsd-build-test3/src/iptsd"
for p in touch-services refresh-iptsd-input-nodes collect-ghost-touch trial-touch-threshold; do
    cp "$ROOT/probe/test2/$p" "$S/bin/$p"
    sh -n "$S/bin/$p"
done
chmod 755 "$S/bin/"*
chmod 644 "$S/etc/iptsd.conf"
python3 "$ROOT/scripts/cpio-list.py" "$S" > "$S.list"
"$B/kernel-android-out/usr/gen_init_cpio" -t 1792972800 "$S.list" | gzip -n -6 > "$ROOT/out/touch/native-touch-fixup.cpio.gz"
sha256sum "$S/bin/iptsd" "$ROOT/out/touch/native-touch-fixup.cpio.gz"
