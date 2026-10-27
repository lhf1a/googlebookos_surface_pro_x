#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=/root/.cache/sq2-probe-build
STAGE=$(mktemp -d "$B/android-initramfs.XXXXXX")
cp -a "$(cat "$ROOT/out/test2/stage-path.txt")/." "$STAGE/"
# This fresh, private build tree is the only deletion target.
test "$(realpath "$STAGE")" != "$B" && [[ "$STAGE" == "$B/android-initramfs."* ]]
rm -rf "$STAGE/lib/modules"
make -C "$B/kernel" O="$B/kernel-android-out" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- INSTALL_MOD_PATH="$STAGE" INSTALL_MOD_STRIP=1 modules_install
find "$STAGE/lib/modules" -type l -delete
for f in kexec-loader android-namespace binder-setup; do
    aarch64-linux-gnu-gcc -static -O2 -Wall -Wextra -Werror "$ROOT/probe/test2/$f.c" -o "$STAGE/bin/$f"
done
cp "$ROOT/probe/test2/"{android-inner,run-googlebook} "$STAGE/bin/"
gcc -O2 -Wall -Wextra -Werror -I"$B/googlebook/libsepol-3.11/include" "$ROOT/scripts/android-host-policy.c" "$B/googlebook/libsepol-3.11/src/libsepol.a" -o "$B/googlebook/android-host-policy"
"$B/googlebook/android-host-policy" "$B/googlebook/vendor/etc/selinux/precompiled_sepolicy" "$STAGE/etc/android-test-policy"
chmod 755 "$STAGE/bin/"*
python3 - "$STAGE" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1])
init=(p/'init').read_text()
init=init.replace('revision=c;','revision=android1;')
init=init.replace('    run-tests "$profile"','    touch-services > /logs/touch-service-startup.txt 2>&1\n    echo "Android environment ready. Run run-googlebook over SSH."')
(p/'init').write_text(init)
network=(p/'bin/network-up').read_text()
network=network.replace('ip link set "$dev" up','if [ "${1:-}" != ethernet ]; then ip link set "$dev" address 12:0b:63:bc:f7:15; fi\nip link set "$dev" up')
(p/'bin/network-up').write_text(network)
PY
python3 "$ROOT/scripts/cpio-list.py" "$STAGE" > "$B/android-initramfs.list"
"$B/kernel-android-out/usr/gen_init_cpio" -t 1792972800 "$B/android-initramfs.list" | gzip -n -6 > "$ROOT/out/android-test/initramfs.cpio.gz"
cat "$ROOT/out/android-test/initramfs.cpio.gz" "$ROOT/out/test2/wifi-addon.cpio.gz" > "$ROOT/out/android-test/combined-initramfs.cpio.gz"
cp "$STAGE/bin/kexec-loader" "$ROOT/out/android-test/"
echo "$STAGE" > "$ROOT/out/android-test/stage-path.txt"
echo 'PACK_ANDROID_PASS'
