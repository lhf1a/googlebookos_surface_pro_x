#!/bin/sh
# Exercise the kernel governor with emulated temperature, not physical heating.
set -eu
sensor=
cooler=
for z in /sys/class/thermal/thermal_zone*; do
    [ -f "$z/type" ] || continue
    [ "$(cat "$z/type")" != gpu-thermal-top ] || sensor=$z
done
for c in /sys/class/thermal/cooling_device*; do
    [ "$(cat "$c/type")" != devfreq-2c00000.gpu ] || cooler=$c
done
test -n "$sensor"
test -n "$cooler"
test -d /sys/firmware/devicetree/base/thermal-zones/gpu-thermal-top/trips/sq2-gpu-passive
test -f "$sensor/emul_temp"
trap 'echo 0 > "$sensor/emul_temp"' EXIT
trap 'exit 130' HUP INT TERM
echo "GPU_COOLING_BEFORE $(cat "$cooler/cur_state") temperature=$(cat "$sensor/temp")"
echo 76000 > "$sensor/emul_temp"
sleep 3
state=$(cat "$cooler/cur_state")
echo "GPU_COOLING_EMULATED_76C state=$state"
test "$state" -gt 0
echo 0 > "$sensor/emul_temp"
sleep 3
echo "GPU_COOLING_RESTORED $(cat "$cooler/cur_state") temperature=$(cat "$sensor/temp")"
test "$(cat "$cooler/cur_state")" -eq 0
echo GPU_COOLING_TEST_PASS
