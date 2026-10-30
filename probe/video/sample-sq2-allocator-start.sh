#!/bin/sh
# Bounded read-only snapshots, launched from the retained initramfs.
set -eu
for delay in 5 10 10; do
 sleep "$delay"
 echo SNAPSHOT
 cat /proc/uptime
 pid=$(nsenter -t 1 -m -r -- /system/bin/getprop init.svc_debug_pid.vendor.graphics.allocator)
 case "$pid" in ''|*[!0-9]*) echo NO_VALID_PID; continue ;; esac
 echo "ALLOCATOR_PID=$pid"
 readlink "/proc/$pid/exe" || true
 grep -E 'libbinder|libminigbm' "/proc/$pid/maps" || true
 timeout 3 nsenter -t 1 -m -r -- /system/bin/service check android.hardware.graphics.allocator.IAllocator/default || true
done
echo SNAPSHOTS_DONE
