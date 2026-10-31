#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=/root/.cache/sq2-probe-build
S=$(mktemp -d "$B/internal-addon.XXXXXX")
O="$ROOT/out/internal-ssd"
# The screen-only userspace policy must ship with its guarded kernel, not an
# older cached Image that would still enter s2idle after display timeout.
grep -aq 'SQ2 screen-only policy: system suspend blocked' "$ROOT/out/screen-only/Image"
cmp "$ROOT/out/screen-only/kernel.config" "$B/kernel-android-out/.config"
cp "$ROOT/out/screen-only/Image" "$O/Image"
test "$(sha256sum "$ROOT/out/android-test/native-combined-initramfs.cpio.gz" | cut -d' ' -f1)" = ae758cea2a36972b23b9d950e03615be2317fd54f514dce4fb8e849e1f3a710b
test "$(sha256sum "$ROOT/out/touch/native-touch-fixup.cpio.gz" | cut -d' ' -f1)" = 83fcc4de064f9874536d4c960fe4b85653c3fbdbbfb0c80a03d4672a753ba949
make -s -C "$B/kernel" O="$B/kernel-android-out" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- INSTALL_MOD_PATH="$S" INSTALL_MOD_STRIP=1 modules_install
find "$S/lib/modules" -type l -delete
cp "$ROOT/out/audio-sequencing/snd-soc-wsa881x.ko" "$S/lib/modules/6.3.8-sq2-android1/kernel/sound/soc/codecs/"
cp "$ROOT/out/audio-sequencing/snd-soc-sdm845.ko" "$S/lib/modules/6.3.8-sq2-android1/kernel/sound/soc/qcom/"
mkdir -p "$S/bin" "$S/etc/sq2-audio"
mkdir -p "$S/etc/googlebook-security/lib64/egl"
# Include the tested multi-plane mapper fix in subsequent complete SSD images.
cp "$ROOT/out/sq2-graphics/runtime/libEGL_freedreno.so" "$S/etc/googlebook-security/lib64/egl/"
cp "$ROOT/probe/test2/init-googlebook-native" "$S/init"
for name in audio-framework-boot audio-speaker-route lockscreen-framework-boot page-mode-notification-setup ssh-framework-boot prepare-native-ssd apply-security-overlay apply-device-info audio-drivers-start collect-audio-crash test-speaker-readiness lab-input-posture lab-keep-awake lab-local-setup skip-google-setup wifi-framework-boot wifi-rollback bluetooth-framework-boot bluetooth-input-watch power-input-boot refresh-sq2-power-input-nodes cover-input-watch touch-framework-ready touch-resume-watch test-sq2-pm-stage; do
    cp "$ROOT/probe/test2/$name" "$S/bin/"
    sh -n "$S/bin/$name"
done
mkdir -p "$S/etc/sq2-power" "$S/etc/googlebook-security/bin/hw" "$S/etc/googlebook-security/etc/init"
cp "$ROOT/out/power-connectivity-20261027/"{surface_aggregator_tabletsw,gpio_keys,pm8941-pwrkey}.ko "$S/etc/sq2-power/"
cp "$ROOT/out/connectivity/sq2-hci-bridge" "$S/bin/"
cp "$ROOT/out/connectivity/com.google.cf.bt/bin/hw/android.hardware.bluetooth-service.cuttlefish" "$S/etc/googlebook-security/bin/hw/sq2-bluetooth"
chmod 755 "$S/etc/googlebook-security/bin/hw/sq2-bluetooth"
cp "$ROOT/configs/sq2-bluetooth.rc" "$S/etc/googlebook-security/etc/init/android.hardware.bluetooth@aidl-service-qti.rc"
cp "$ROOT/out/audio-lte/bin/"{tinymix,tinyplay,tinypcminfo,sq2-qmi-readonly} "$S/bin/"
NDK=/root/.cache/sq2-graphics/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin
"$NDK/aarch64-linux-android35-clang" -O2 -Wall -Wextra -Werror "$ROOT/probe/test2/sq2-audio-framework-test.c" -laaudio -lm -o "$S/bin/sq2-audio-framework-test"
cp "$O/"sq2-ssd-* "$S/etc/"
cp "$ROOT/configs/primary_audio_policy_sq2.xml" "$S/etc/sq2-audio/primary_audio_policy_configuration.xml"
python3 "$ROOT/scripts/patch-sq2-audio-hal.py" "$ROOT/out/audio-lte/audio-hal-original" "$S/etc/sq2-audio/audio-hal"
chmod 755 "$S/init" "$S/bin/"* "$S/etc/sq2-audio/audio-hal"
sh -n "$S/init"
python3 "$ROOT/scripts/cpio-list.py" "$S" > "$S.list"
"$B/kernel-android-out/usr/gen_init_cpio" -t 1793059200 "$S.list" | gzip -n -6 > "$O/internal-addon.cpio.gz"
cat "$ROOT/out/android-test/native-combined-initramfs.cpio.gz" "$ROOT/out/touch/native-touch-fixup.cpio.gz" "$O/internal-addon.cpio.gz" > "$O/native-combined-initramfs.cpio.gz"
echo "$S" > "$O/addon-stage-path.txt"
sha256sum "$O/"{Image,sc8180x-surface-pro-x.dtb,native-combined-initramfs.cpio.gz}
