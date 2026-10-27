#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=/root/.cache/sq2-probe-build
STAGE=$(mktemp -d "$B/googlebook-native.XXXXXX")
cp -a "$(cat "$ROOT/out/test2/stage-path.txt")/." "$STAGE/"
cp "$ROOT/configs/iptsd-sq2.conf" "$STAGE/etc/iptsd.conf"
cp "$ROOT/out/test2/iptsd/iptsd" "$STAGE/bin/iptsd"
for p in touch-services refresh-iptsd-input-nodes collect-ghost-touch trial-touch-threshold; do
    cp "$ROOT/probe/test2/$p" "$STAGE/bin/"
    chmod 755 "$STAGE/bin/$p"
done
chmod 755 "$STAGE/bin/iptsd"
# QCA Bluetooth firmware from the same staged, provenance-checked SQ2 set.
cp -a "$ROOT/out/test2/firmware/qca" "$STAGE/lib/firmware/"
# Surface WCN3998 ROM reports 01; the pinned device tree documents 21 firmware.
ln -sfn crbtfw21.tlv "$STAGE/lib/firmware/qca/crbtfw01.tlv"
ln -sfn crnv21.bin "$STAGE/lib/firmware/qca/crnv01.bin"
test "$(realpath "$STAGE")" != "$B" && [[ "$STAGE" == "$B/googlebook-native."* ]]
rm -rf "$STAGE/lib/modules"
make -s -C "$B/kernel" O="$B/kernel-android-out" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- INSTALL_MOD_PATH="$STAGE" INSTALL_MOD_STRIP=1 modules_install
find "$STAGE/lib/modules" -type l -delete
for f in native-handoff boot-kmsg binder-setup sparse-restore memfd-compat sq2-loop-slice; do
    aarch64-linux-gnu-gcc -static -O2 -Wall -Wextra -Werror "$ROOT/probe/test2/$f.c" -o "$STAGE/bin/$f"
done
gcc -O2 -Wall -Wextra -Werror -I"$B/googlebook/libsepol-3.11/include" "$ROOT/scripts/android-host-policy.c" "$B/googlebook/libsepol-3.11/src/libsepol.a" -o "$B/googlebook/android-host-policy"
"$B/googlebook/android-host-policy" "$B/googlebook/vendor/etc/selinux/precompiled_sepolicy" "$STAGE/etc/android-native-test-policy" --all-types
cp "$ROOT/probe/test2/init-googlebook-native" "$STAGE/init"
cp "$ROOT/probe/test2/fstab.sq2-native" "$STAGE/etc/"
cp "$ROOT/probe/test2/create-test-bootmeta" "$STAGE/bin/"
chmod 755 "$STAGE/bin/create-test-bootmeta"
cp "$ROOT/out/android-test/userdata.sparse.gz" "$STAGE/etc/googlebook-userdata.sparse.gz"
cp "$ROOT/out/android-test/bootmeta.sparse.gz" "$STAGE/etc/googlebook-bootmeta.sparse.gz"
cp "$ROOT/out/android-test/bootmeta-layout" "$STAGE/etc/googlebook-bootmeta-layout"
cp "$ROOT/probe/test2/"{native-monitor,android-console,prepare-native-gpu,keep-debug-route,collect-framework,lab-keep-awake} "$STAGE/bin/"
chmod 755 "$STAGE/bin/"{native-monitor,android-console,prepare-native-gpu,keep-debug-route,collect-framework,lab-keep-awake}
cp "$ROOT/probe/test2/"{wifi-framework-boot,wifi-rollback,skip-google-setup,lab-input-posture} "$STAGE/bin/"
chmod 755 "$STAGE/bin/"{wifi-framework-boot,wifi-rollback,skip-google-setup,lab-input-posture}
cp "$ROOT/private/sq2-android-wifi.conf" "$STAGE/etc/sq2-android-wifi.conf"
chmod 600 "$STAGE/etc/sq2-android-wifi.conf"
chmod 755 "$STAGE/init"
if [ -f "$ROOT/out/ssd/sq2-ssd-layout" ]; then
    cp "$ROOT/out/ssd/"sq2-ssd-* "$STAGE/etc/"
    cp "$ROOT/probe/test2/prepare-native-ssd" "$STAGE/bin/"
    chmod 755 "$STAGE/bin/prepare-native-ssd"
fi
if [[ "${1:-}" == --software-security || "${1:-}" == --graphics ]]; then
    python3 "$ROOT/scripts/build-googlebook-security-overlay.py"
    cp -a "$(cat "$ROOT/out/android-test/security-overlay-path.txt")" "$STAGE/etc/googlebook-security"
    cp "$ROOT/probe/test2/apply-security-overlay" "$STAGE/bin/"
    chmod 755 "$STAGE/bin/apply-security-overlay"
    if [[ "$1" == --graphics ]]; then
        python3 "$ROOT/scripts/build-googlebook-graphics-overlay.py"
        cp -a "$(cat "$ROOT/out/android-test/graphics-overlay-path.txt")/." "$STAGE/etc/googlebook-security/"
    fi
elif [[ -n "${1:-}" ]]; then
    echo "Unknown pack option: $1" >&2
    exit 1
fi
python3 - "$STAGE" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1])/'bin/network-up'
s=p.read_text().replace('ip link set "$dev" up','if [ "${1:-}" != ethernet ]; then ip link set "$dev" address 12:0b:63:bc:f7:15; fi\nip link set "$dev" up')
p.write_text(s)
PY
python3 "$ROOT/scripts/cpio-list.py" "$STAGE" > "$B/googlebook-native.list"
"$B/kernel-android-out/usr/gen_init_cpio" -t 1792972800 "$B/googlebook-native.list" | gzip -n -6 > "$ROOT/out/android-test/native-initramfs.cpio.gz"
cat "$ROOT/out/android-test/native-initramfs.cpio.gz" "$ROOT/out/test2/wifi-addon.cpio.gz" > "$ROOT/out/android-test/native-combined-initramfs.cpio.gz"
echo "$STAGE" > "$ROOT/out/android-test/native-stage-path.txt"
echo PACK_GOOGLEBOOK_NATIVE_PASS
