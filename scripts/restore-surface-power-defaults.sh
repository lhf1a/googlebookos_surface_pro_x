#!/system/bin/sh
# Explicit one-time operation inside Android. Preserve credentials and later user choices.
set -eu
user_id=$(am get-current-user)
case "$user_id" in ''|*[!0-9]*) exit 1;; esac
# Deleting overrides lets framework/provider defaults apply; no guessed timeouts.
settings --user "$user_id" delete system screen_off_timeout
settings --user "$user_id" delete secure lock_screen_lock_after_timeout
settings --user "$user_id" delete secure sleep_timeout
settings delete global stay_on_while_plugged_in
cmd overlay fabricate --target android --name Sq2Lockscreen android:bool/config_disableLockscreenByDefault 0x12 0
cmd overlay enable --user 0 com.android.shell:Sq2Lockscreen
if [ "$user_id" != 0 ]; then cmd overlay enable --user "$user_id" com.android.shell:Sq2Lockscreen; fi
cmd lock_settings set-disabled --user "$user_id" false
cmd lock_settings get-disabled --user "$user_id"
echo ANDROID_POWER_DEFAULTS_RESTORED
