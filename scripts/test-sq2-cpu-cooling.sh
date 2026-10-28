#!/bin/sh
# Validate both CPU cooling maps with emulation, never physical overheating.
set -eu
sensor=
cleanup() { [ -z "$sensor" ] || echo 0 > "$sensor/emul_temp"; }
trap cleanup EXIT
trap 'exit 130' HUP INT TERM
for pair in 0:0 1:4; do
    cluster=${pair%:*};cpu=${pair#*:};sensor=;cooler=
    for z in /sys/class/thermal/thermal_zone*; do
        [ "$(cat "$z/type")" != "cluster$cluster-thermal" ] || sensor=$z
    done
    for c in /sys/class/thermal/cooling_device*; do
        [ "$(cat "$c/type")" != "cpufreq-cpu$cpu" ] || cooler=$c
    done
    test -n "$sensor";test -n "$cooler"
    echo "CLUSTER$cluster BEFORE=$(cat "$cooler/cur_state") temp=$(cat "$sensor/temp")"
    echo 81000 > "$sensor/emul_temp"
    sleep 3
    state=$(cat "$cooler/cur_state")
    echo "CLUSTER$cluster EMULATED_81C=$state max=$(cat /sys/devices/system/cpu/cpufreq/policy$cpu/scaling_max_freq)"
    test "$state" -gt 0
    echo 0 > "$sensor/emul_temp"
    sleep 3
    echo "CLUSTER$cluster RESTORED=$(cat "$cooler/cur_state")"
    test "$(cat "$cooler/cur_state")" -eq 0
done
echo CPU_COOLING_TEST_PASS
