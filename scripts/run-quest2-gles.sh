#!/bin/sh
# Run from retained initramfs SSH root. Only the private probe sees these libs.
# Default is offscreen. --ahb reproduces a known GPU-hang failure on this donor;
# do not use it as a routine smoke test or run concurrently with other GPU tests.
set -eu
base=/data/local/tmp/sq2-closed/gles-v77
ion=1
if [ "${1:-}" = --gsl-dmabuf ]; then
    base=/data/local/tmp/sq2-closed/gles-v819-dmabuf
    ion=0
    shift
fi
case "${1:-}" in ''|--ahb|--ahb-import-only) ;; *) echo 'usage: run-quest2-gles.sh [--gsl-dmabuf] [--ahb|--ahb-import-only]' >&2; exit 2;; esac
test "$#" -le 1
root=/proc/1/root
test "$(cat /sys/module/suspend/parameters/screen_only)" = Y
expected=0b36eeba9d77ed15e7465396314df80a9479f0b5ccd858d362b40af39fb1759c
actual=$(sha256sum "$root/vendor/lib64/hw/vulkan.freedreno.so" | cut -d ' ' -f1)
test "$actual" = "$expected"
(cd "$root$base"; sha256sum -c SHA256SUMS; sha256sum -c PROBE_SHA256SUMS)
mkdir /tmp/sq2-quest2-gles.lock
trap 'rmdir /tmp/sq2-quest2-gles.lock' EXIT
boot=$(cat /proc/sys/kernel/random/boot_id)
echo "BOOT_BEFORE=$boot"
nsenter -t 1 -m -r -- /system/bin/pidof system_server surfaceflinger
result=0
preload="$base/libqti-mapper3-bridge.so"
if [ "$ion" = 1 ]; then preload="$base/libquest2-ion-dmaheap.so:$preload";fi
timeout -k 3 25 nsenter -t 1 -m -r -- /system/bin/env \
  LD_PRELOAD="$preload" \
  LD_LIBRARY_PATH="$base:/system/lib64:/vendor/lib64" \
  "$base/gles-readback" "$@" || result=$?
echo "PROBE_EXIT=$result"
test "$(cat /proc/sys/kernel/random/boot_id)" = "$boot"
nsenter -t 1 -m -r -- /system/bin/pidof system_server surfaceflinger
exit "$result"
