#!/system/bin/sh
# Run once as root INSIDE Android (not the diagnostic initramfs chroot).
# Does not create, clear or change credentials. Not a boot-time policy enforcer.
set -eu
user_id=$(am get-current-user)
case "$user_id" in
    ''|*[!0-9]*) echo 'Cannot determine Android foreground user' >&2; exit 1 ;;
esac
cmd overlay fabricate --target android --name Sq2Lockscreen \
    android:bool/config_disableLockscreenByDefault 0x12 0
cmd overlay enable --user 0 com.android.shell:Sq2Lockscreen
if [ "$user_id" != 0 ]; then
    cmd overlay enable --user "$user_id" com.android.shell:Sq2Lockscreen
fi
cmd lock_settings set-disabled --user "$user_id" false
cmd overlay lookup --user "$user_id" android android:bool/config_disableLockscreenByDefault
echo 'Expected resource value: false. Check screen off/on and unlock on the device.'
