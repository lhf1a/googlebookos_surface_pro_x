#!/bin/sh
# Run from the retained initramfs SSH root. Restore devfreq limits on exit.
set -eu
frequency=${1:?frequency in Hz required}
case "$frequency" in 514000000|590000000|661000000) ;; *) exit 2;; esac
test "$(cat /sys/module/suspend/parameters/screen_only)" = Y
dev=/sys/class/devfreq/2c00000.gpu
grep -qw "$frequency" "$dev/available_frequencies"
old_min=$(cat "$dev/min_freq")
old_max=$(cat "$dev/max_freq")
gpu_sensors=0
for zone in /sys/class/thermal/thermal_zone*; do
    [ -f "$zone/type" ] || continue
    case "$(cat "$zone/type")" in *gpu*) cat "$zone/temp" >/dev/null;gpu_sensors=$((gpu_sensors+1));; esac
done
[ "$gpu_sensors" -gt 0 ] || { echo 'No GPU temperature sensor; refusing load test';exit 1; }
child=
restore() {
    [ -z "$child" ] || kill "$child" 2>/dev/null || true
    echo "$old_min" > "$dev/min_freq"
    echo "$old_max" > "$dev/max_freq"
}
trap restore EXIT
trap 'exit 130' INT TERM HUP
echo "$frequency" > "$dev/max_freq"
echo "$frequency" > "$dev/min_freq"
echo "GPU_TEST_START requested_hz=$frequency"
cat /proc/sys/kernel/random/boot_id
pidof surfaceflinger system_server
timeout 40 env SQ2_VULKAN_SECONDS=30 SQ2_VULKAN_ITERATIONS=4096 \
    nsenter -t 1 -m -r -- /data/local/tmp/vulkan-readback /system/lib64/libvulkan.so &
child=$!
while kill -0 "$child" 2>/dev/null; do
    printf 'devfreq_reported_hz='
    cat "$dev/cur_freq"
    for zone in /sys/class/thermal/thermal_zone*; do
        [ -f "$zone/type" ] || continue
        case "$(cat "$zone/type")" in
            *gpu*)
                temp=$(cat "$zone/temp")
                printf '%s=%s\n' "$(cat "$zone/type")" "$temp"
                if [ "$temp" -ge 80000 ]; then
                    echo GPU_TEST_THERMAL_STOP
                    exit 1
                fi
            ;;
        esac
    done
    sleep 2
done
wait "$child"
child=
pidof surfaceflinger system_server
echo GPU_TEST_PASS
