#!/bin/sh
# Retained initramfs root; only operates the separate installed test package.
# Run with the display awake and unlocked. No screen/suspend/clock changes.
set -eu
pkg=org.surface.sq2.quest2probe
# Keep executable and arguments separate for busybox nsenter.
android() { exe=$1;shift;nsenter -t 1 -m -r -- "/system/bin/$exe" "$@"; }
test "$(cat /sys/module/suspend/parameters/screen_only)" = Y
user=$(android am get-current-user)
boot=$(cat /proc/sys/kernel/random/boot_id)
pids=$(android pidof system_server surfaceflinger)
sensor=
for z in /sys/class/thermal/thermal_zone*; do
    case "$(cat "$z/type")" in gpu-thermal-top) sensor=$z/temp;; esac
done
test -n "$sensor"
log=$(mktemp /tmp/quest2-wsi.XXXXXX)
logger=
stop_logger() { if [ -n "$logger" ];then kill "$logger" 2>/dev/null || true;wait "$logger" 2>/dev/null || true;logger=;fi; }
cleanup() { stop_logger;android am force-stop --user "$user" "$pkg" >/dev/null 2>&1 || true;rm -f "$log"; }
trap cleanup EXIT
trap 'exit 130' INT TERM HUP
wait_frames() {
    expected=$1
    attempt=0
    while [ "$attempt" -lt 30 ]; do
        test "$boot" = "$(cat /proc/sys/kernel/random/boot_id)"
        test "$pids" = "$(android pidof system_server surfaceflinger)"
        test "$app" = "$(android pidof "$pkg")"
        temp=$(cat "$sensor");echo "GPU_MILLIC=$temp"
        test "$temp" -lt 80000
        if grep -q 'FAIL' "$log";then cat "$log";return 1;fi
        count=$(grep -c 'SQ2_VULKAN_WSI_OK frames=1800' "$log" || true)
        if [ "$count" -ge "$expected" ];then
            grep -E 'CLOSED_LOADER_READY|DEVICE |READY surface|WSI_OK|APP_CMD 2$' "$log"
            grep -q 'vulkan-bridge.so' "/proc/$app/maps"
            grep -q 'libqti-mapper-bridge.so' "/proc/$app/maps"
            return 0
        fi
        sleep 2;attempt=$((attempt+1))
    done
    cat "$log";echo WSI_TIMEOUT;return 1
}
for round in 1 2;do
    stop_logger
    android am force-stop --user "$user" "$pkg"
    android am start --user "$user" -W -n "$pkg/android.app.NativeActivity"
    app=$(android pidof "$pkg")
    test -n "$app"
    nsenter -t 1 -m -r -- /system/bin/logcat --pid="$app" -s SQ2Vulkan:I >"$log" &
    logger=$!
    echo "COLD_ROUND=$round APP_PID=$app"
    wait_frames 1
    if [ "$round" = 1 ];then
        android input keyevent 3
        sleep 3
        grep -q 'APP_CMD 2$' "$log" # real window destruction, not just focus loss
        android am start --user "$user" -W -n "$pkg/android.app.NativeActivity"
        wait_frames 2
    fi
done
echo QUEST2_WSI_COLD_AND_WINDOW_RECREATE_OK
