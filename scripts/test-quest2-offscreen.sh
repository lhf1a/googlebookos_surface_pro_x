#!/bin/sh
# Retained initramfs SSH root. Run only the isolated, profile-skipped donor.
# No clocks, kernel modules, graphics services or installed HALs are changed.
set -eu
base=${1:-/data/local/tmp/sq2-closed}
case "$base" in /data/local/tmp/*) ;; *) echo 'Expected Android local test directory' >&2;exit 2;; esac
test "$(cat /sys/module/suspend/parameters/screen_only)" = Y
test -x "/proc/1/root$base/probe"
test -f "/proc/1/root$base/v819/vulkan-no-profile.so"
sensor=
for zone in /sys/class/thermal/thermal_zone*; do
    test -f "$zone/type" || continue
    case "$(cat "$zone/type")" in gpu-thermal-top) sensor=$zone/temp;; esac
done
test -n "$sensor" || { echo 'No GPU temperature sensor' >&2;exit 1; }
boot_before=$(cat /proc/sys/kernel/random/boot_id)
pids_before=$(nsenter -t 1 -m -r -- /system/bin/pidof system_server surfaceflinger)
child=
cleanup() { if [ -n "$child" ]; then kill "$child" 2>/dev/null || true;wait "$child" 2>/dev/null || true;fi; }
trap cleanup EXIT
trap 'exit 130' INT TERM HUP
for run in 1 2 3; do
    test "$(cat "$sensor")" -lt 80000
    echo "OFFSCREEN_RUN=$run"
    timeout 25 nsenter -t 1 -m -r -- /system/bin/env \
        LD_LIBRARY_PATH="$base/v819:/system/lib64:/vendor/lib64" \
        SQ2_VULKAN_SECONDS=10 SQ2_VULKAN_ITERATIONS=4096 \
        "$base/probe" "$base/v819/vulkan-no-profile.so" --offscreen-only &
    child=$!
    while kill -0 "$child" 2>/dev/null; do
        temp=$(cat "$sensor")
        echo "GPU_MILLIC=$temp"
        if [ "$temp" -ge 80000 ]; then echo THERMAL_STOP;exit 1;fi
        sleep 2
    done
    wait "$child"
    child=
done
test "$boot_before" = "$(cat /proc/sys/kernel/random/boot_id)"
test "$pids_before" = "$(nsenter -t 1 -m -r -- /system/bin/pidof system_server surfaceflinger)"
echo 'ISOLATED_OFFSCREEN_REPEAT_OK; AHB/WSI not covered'
