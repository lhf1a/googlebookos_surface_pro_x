#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUNDLE=${1:-"$ROOT/out/test2/iptsd"}
python3 - "$BUNDLE" "$ROOT" <<'PY'
import hashlib, json, pathlib, sys
p = pathlib.Path(sys.argv[1])
root = pathlib.Path(sys.argv[2])
m = json.loads((p/'build-options.json').read_text())
expected = {'on':['patch-iptsd-sq2-touch.py', 'patch-iptsd-sq2-heatmap.py'],
            'off':[], 'edge-only':['patch-iptsd-sq2-heatmap.py']}[m['ghost_touch_fix']]
assert m.get('touch_calibration') in ('on', 'off'), 'Rebuild IPTSd with explicit calibration metadata'
if m['touch_calibration'] == 'on':
    expected += ['patch-iptsd-sq2-calibration.py']
    assert m['calibration_header_sha256'] == hashlib.sha256((root/'probe/test2/sq2-touch-calibration.hpp').read_bytes().replace(b'\r\n',b'\n')).hexdigest()
assert m['patches'] == expected
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
