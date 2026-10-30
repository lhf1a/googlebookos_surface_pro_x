#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUNDLE=${1:-"$ROOT/out/test2/iptsd"}
python3 - "$BUNDLE" <<'PY'
import hashlib, json, pathlib, sys
p = pathlib.Path(sys.argv[1])
m = json.loads((p/'build-options.json').read_text())
expected = ['patch-iptsd-sq2-touch.py', 'patch-iptsd-sq2-heatmap.py'] if m['ghost_touch_fix'] == 'on' else []
assert m['ghost_touch_fix'] in ('on', 'off') and m['patches'] == expected
for n in ('iptsd', 'iptsd.conf'):
    assert hashlib.sha256((p/n).read_bytes()).hexdigest() == m['sha256'][n], n
PY
B=/root/.cache/sq2-probe-build
S=$(mktemp -d "$B/touch-amend.XXXXXX")
mkdir -p "$S/bin" "$S/etc"
cp "$BUNDLE/iptsd.conf" "$S/etc/iptsd.conf"
cp "$BUNDLE/iptsd" "$S/bin/iptsd"
for p in touch-services refresh-iptsd-input-nodes collect-ghost-touch trial-touch-threshold; do
    cp "$ROOT/probe/test2/$p" "$S/bin/$p"
    sh -n "$S/bin/$p"
done
chmod 755 "$S/bin/"*
chmod 644 "$S/etc/iptsd.conf"
python3 "$ROOT/scripts/cpio-list.py" "$S" > "$S.list"
"$B/kernel-android-out/usr/gen_init_cpio" -t 1792972800 "$S.list" | gzip -n -6 > "$ROOT/out/touch/native-touch-fixup.cpio.gz"
sha256sum "$S/bin/iptsd" "$ROOT/out/touch/native-touch-fixup.cpio.gz"
