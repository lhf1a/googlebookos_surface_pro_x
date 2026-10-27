#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=/root/.cache/sq2-probe-build
O="$ROOT/out/power-connectivity-20261027"
S=$(mktemp -d "$B/power-connectivity.XXXXXX")
mkdir -p "$S/bin" "$S/etc/sq2-power" "$S/etc/googlebook-security/bin/hw" "$S/etc/googlebook-security/etc/init"
cp "$ROOT/probe/test2/init-googlebook-native" "$S/init"
for name in bluetooth-framework-boot power-input-boot lab-input-posture lab-keep-awake refresh-sq2-power-input-nodes cover-input-watch touch-framework-ready touch-resume-watch test-sq2-pm-stage; do
    cp "$ROOT/probe/test2/$name" "$S/bin/"
    sh -n "$S/bin/$name"
done
mkdir -p "$S/lib/modules/6.3.8-sq2-android1/kernel/drivers/hid/spi-hid"
cp "$O/spi-hid.ko" "$S/lib/modules/6.3.8-sq2-android1/kernel/drivers/hid/spi-hid/"
cp "$ROOT/out/connectivity/sq2-hci-bridge" "$S/bin/"
cp "$O/"{surface_aggregator_tabletsw,gpio_keys,pm8941-pwrkey}.ko "$S/etc/sq2-power/"
cp "$ROOT/out/connectivity/com.google.cf.bt/bin/hw/android.hardware.bluetooth-service.cuttlefish" "$S/etc/googlebook-security/bin/hw/sq2-bluetooth"
cp "$ROOT/configs/sq2-bluetooth.rc" "$S/etc/googlebook-security/etc/init/android.hardware.bluetooth@aidl-service-qti.rc"
chmod 755 "$S/init" "$S/bin/"* "$S/etc/googlebook-security/bin/hw/sq2-bluetooth"
sh -n "$S/init"
python3 "$ROOT/scripts/cpio-list.py" "$S" > "$S.list"
"$B/kernel-android-out/usr/gen_init_cpio" -t 1793059200 "$S.list" | gzip -n -6 > "$O/power-connectivity-addon.cpio.gz"
sha256sum "$O/power-connectivity-addon.cpio.gz"
