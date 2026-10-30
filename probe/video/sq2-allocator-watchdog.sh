#!/bin/sh
# Run from the retained initramfs root via setsid, NOT Android's root.
set -eu
mode=${1:-}
case "$mode" in observe) delay=5 ;; restore|restore-scoped) delay=45 ;; restore-main10) delay=180 ;; *) exit 2 ;; esac
# Optional per-attempt label preserves prior evidence on the same boot.
# No label retains the original path and collision refusal.
suffix=
[ "$#" -le 2 ] || exit 2
if [ "$#" -eq 2 ]; then
 case "$2" in ''|*[!a-zA-Z0-9_-]*) exit 2 ;; esac
 [ "${#2}" -le 48 ] || exit 2
 suffix=-$2
fi
[ "$(id -u)" = 0 ]
[ "$(cat /sys/module/suspend/parameters/screen_only)" = Y ]
[ "$(stat -c %d:%i /)" != "$(stat -c %d:%i /proc/1/root/.)" ]
while IFS=: read -r hierarchy controllers group; do
 [ "$group" = / ]
done </proc/self/cgroup
boot=$(cat /proc/sys/kernel/random/boot_id)
run=/proc/1/root/data/local/tmp/sq2-allocator-watch-$mode-$boot$suffix
mkdir "$run"
exec >"$run/watchdog.log" 2>&1
echo "$$" >"$run/pid"
target=/proc/1/root/vendor/bin/hw/android.hardware.graphics.allocator-service.minigbm
backup=/proc/1/root/vendor/bin/hw/sq2-allocator-original
original=7f394d7082735401e0a2cdc9fbcb010edef49a4d703366a9a73ef295eeb919da
launcher=249da7cae6e999059e0e03a14728b7249681a9c2ecbf57751e8ded4ed276e02e
case "$mode" in restore-scoped|restore-main10) launcher=c859a01459841b8e7a8caff77981dd5d4bd30f13311c0f50e12bc07e6c7416b1 ;; esac
digest() { sha256sum "$1" | cut -d ' ' -f 1; }
[ "$(digest "$target")" = "$original" ]
if [ "$mode" != observe ]; then
 [ "$(digest "$backup")" = "$original" ]
fi
cat /proc/self/cgroup
echo "WATCHDOG_READY pid=$$ mode=$mode delay=$delay boot=$boot"
sync
sleep "$delay"
[ "$(cat /proc/sys/kernel/random/boot_id)" = "$boot" ]
actual=$(digest "$target")
if [ "$mode" = observe ]; then
 [ "$actual" = "$original" ]
 echo OBSERVE_FINISHED_NO_CHANGE
 exit 0
fi
if [ "$actual" = "$original" ]; then
 echo ORIGINAL_ALREADY_PRESENT_NO_RESTART
 exit 0
fi
[ "$actual" = "$launcher" ] || { echo UNKNOWN_TARGET_REFUSED; exit 3; }
[ "$(digest "$backup")" = "$original" ]
test ! -e "$target.watchdog-new"
cp "$backup" "$target.watchdog-new"
chmod 755 "$target.watchdog-new"
[ "$(digest "$target.watchdog-new")" = "$original" ]
sync
mv "$target.watchdog-new" "$target"
sync
echo ORIGINAL_RESTORED
nsenter -t 1 -m -r -- /system/bin/setprop ctl.restart vendor.graphics.allocator
echo RESTART_REQUESTED
