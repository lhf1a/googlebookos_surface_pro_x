#!/system/bin/sh
# Restore only the known RAM allocator launcher; leave global libraries alone.
set -eu
[ "$(id -u)" = 0 ]
[ "$(cat /sys/module/suspend/parameters/screen_only)" = Y ]
target=/vendor/bin/hw/android.hardware.graphics.allocator-service.minigbm
backup=/vendor/bin/hw/sq2-allocator-original
original=7f394d7082735401e0a2cdc9fbcb010edef49a4d703366a9a73ef295eeb919da
launcher=249da7cae6e999059e0e03a14728b7249681a9c2ecbf57751e8ded4ed276e02e
digest() { sha256sum "$1" | cut -d ' ' -f 1; }
actual=$(digest "$target")
if [ "$actual" = "$original" ]; then
 echo ORIGINAL_ALREADY_PRESENT_NO_RESTART
 exit 0
fi
[ "$actual" = "$launcher" ] || [ "$actual" = c859a01459841b8e7a8caff77981dd5d4bd30f13311c0f50e12bc07e6c7416b1 ] || { echo UNKNOWN_TARGET_REFUSED; exit 3; }
[ "$(digest "$backup")" = "$original" ] || { echo INVALID_BACKUP_REFUSED; exit 4; }
test ! -e "$target.restore-new"
cp "$backup" "$target.restore-new"
chmod 755 "$target.restore-new"
[ "$(digest "$target.restore-new")" = "$original" ]
sync
mv "$target.restore-new" "$target"
sync
echo ORIGINAL_RESTORED
setprop ctl.restart vendor.graphics.allocator
sleep 1
getprop init.svc.vendor.graphics.allocator
getprop init.svc.surfaceflinger
