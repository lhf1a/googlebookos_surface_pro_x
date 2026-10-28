#!/bin/sh
# Bounded checked load, preserves schedutil and policy limits.
set -eu
workers=
cleanup() { for pid in $workers; do kill "$pid" 2>/dev/null || :; done; }
trap cleanup EXIT
trap 'exit 130' HUP INT TERM
test "$(cat /sys/module/suspend/parameters/screen_only)" = Y
for policy in 0 4; do
    test "$(cat /sys/devices/system/cpu/cpufreq/policy$policy/scaling_governor)" = schedutil
done
test "$(cat /sys/devices/system/cpu/cpufreq/policy4/scaling_max_freq)" = 3148800
for cpu in 0 1 2 3 4 5 6 7; do
    /tmp/cpu-frequency-check "$cpu" 20 &
    workers="$workers $!"
done
for n in $(seq 1 22); do
    printf 'SAMPLE %s' "$n"
    for policy in 0 4; do
        printf ' policy%s=%s' "$policy" "$(cat /sys/devices/system/cpu/cpufreq/policy$policy/scaling_cur_freq)"
    done
    for zone in /sys/class/thermal/thermal_zone*; do
        case "$(cat "$zone/type")" in
            cluster*-thermal|cpu*-thermal)
                temp=$(cat "$zone/temp")
                printf ' %s=%s' "$(cat "$zone/type")" "$temp"
                test "$temp" -lt 85000 || { echo TEMPERATURE_LIMIT; exit 1; };;
        esac
    done
    echo
    sleep 1
done
for pid in $workers; do wait "$pid"; done
workers=
echo CPU_FREQUENCY_LOAD_PASS
