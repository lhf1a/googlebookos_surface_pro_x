#!/bin/sh
# Retained initramfs root only. A finite RAM-only global display trial.
# No disk partition, boot image, persistent property or saved setting is changed.
set -eu
umask 077
root=/proc/1/root
base=/data/local/tmp/sq2-closed/combined-v1
case "${SQ2_COMBINED_REVISION:-1}" in 1) ;; 3) base=/data/local/tmp/sq2-closed/combined-v3;; *) exit 2;; esac
mesa_gl=f16302e33e4f7c8bbaccbe5ab5fd5d19f3279222ee430ace236c4bad237a5434
mesa_vk=0b36eeba9d77ed15e7465396314df80a9479f0b5ccd858d362b40af39fb1759c
android(){ nsenter -t 1 -m -r -- "$@"; }
hash(){ sha256sum "$1" | cut -d ' ' -f1; }
same_inode(){ test "$(stat -c '%d:%i' "$1")" = "$(stat -c '%d:%i' "$2")"; }
check_mesa(){
    test "$(hash "$root/vendor/lib64/egl/libEGL_freedreno.so")" = "$mesa_gl"
    test "$(hash "$root/vendor/lib64/hw/vulkan.freedreno.so")" = "$mesa_vk"
}
rollback(){
    state=$1
    case "$state" in /tmp/sq2-sf-trial.[0-9]*.[0-9]*) ;; *) exit 2;; esac
    test -d "$state"
    test "$(cat "$state/boot")" = "$(cat /proc/sys/kernel/random/boot_id)"
    if ! mkdir "$state/rolling-back" 2>/dev/null;then return 0;fi
    stage=$(cat "$state/stage")
    case "$stage" in /data/local/tmp/sq2-closed/sf-trial.[0-9]*.[0-9]*) ;; *) exit 2;; esac
    echo SF_TRIAL_ROLLBACK_BEGIN
    android /system/bin/setprop ctl.stop surfaceflinger
    sleep 2
    # Stat identity also covers the window between mounting and recording its
    # mount ID. Never unmount a path merely because a stale state file exists.
    if same_inode "$root/vendor/lib64/hw/vulkan.freedreno.so" "$root$stage/egl/libquest2-vulkan-wrapper.so";then
        android /system/bin/umount /vendor/lib64/hw/vulkan.freedreno.so ||
            android /system/bin/umount -l /vendor/lib64/hw/vulkan.freedreno.so
    fi
    if same_inode "$root/vendor/lib64/egl" "$root$stage/egl";then
        android /system/bin/umount /vendor/lib64/egl || android /system/bin/umount -l /vendor/lib64/egl
    fi
    check_mesa
    android /system/bin/setprop ctl.start surfaceflinger
    n=0
    while [ "$n" -lt 12 ];do
        if android /system/bin/pidof surfaceflinger > "$state/restored-pids";then break;fi
        sleep 1;n=$((n+1))
    done
    check_mesa
    android /system/bin/getprop init.svc.surfaceflinger
    echo SF_TRIAL_MESA_RESTORED
    if [ -d "$root$stage/evidence" ];then
        cp "$state/watchdog.log" "$root$stage/evidence/watchdog.log"
    fi
    touch "$state/complete"
    rmdir /tmp/sq2-sf-trial.lock
}
case "${1:-}" in
    --rollback) test "$#" = 2;rollback "$2";exit;;
    --watchdog)
        test "$#" = 2;watch_state=$2;last=;changes=0;n=0
        watch_stage=$(cat "$watch_state/stage")
        case "$watch_stage" in /data/local/tmp/sq2-closed/sf-trial.[0-9]*.[0-9]*) ;; *) exit 2;; esac
        evidence="$root$watch_stage/evidence"
        mkdir "$evidence"
        cp "$watch_state/boot" "$evidence/boot"
        while [ "$n" -lt 12 ];do
            sleep 5;n=$((n+1))
            current=$(android /system/bin/pidof surfaceflinger || true)
            if [ -n "$current" ] && [ -n "$last" ] && [ "$current" != "$last" ];then changes=$((changes+1));fi
            if [ -n "$current" ];then
                last=$current
                grep -E 'lib(EGL|GLES|gsl|qti|quest2)|vulkan' "/proc/$current/maps" > "$evidence/maps.$n" 2>/dev/null || true
            fi
            printf 'tick=%s sf_pid=%s changes=%s\n' "$n" "$current" "$changes" >> "$evidence/progress"
            cp "$watch_state/watchdog.log" "$evidence/watchdog.log"
            if [ "$changes" -ge 2 ];then echo SF_TRIAL_RESTART_LIMIT;break;fi
        done
        rollback "$watch_state";exit;;
    --start) test "$#" = 1;;
    *) echo 'usage: test-quest2-surfaceflinger.sh --start' >&2;exit 2;;
esac
test "$(id -u)" = 0
test "$(stat -c '%d:%i' /)" != "$(stat -c '%d:%i' "$root")"
test "$(cat /sys/module/suspend/parameters/screen_only)" = Y
check_mesa
test "$(android /system/bin/getprop ro.hardware.egl)" = freedreno
test "$(android /system/bin/getprop ro.hardware.vulkan)" = freedreno
test "$(android /system/bin/getprop sys.boot_completed)" = 1
test "$(android /system/bin/getprop init.svc.surfaceflinger)" = running
(cd "$root$base";sha256sum -c SHA256SUMS)
test "$(find "$root$base" -maxdepth 1 -name '*.so' | wc -l)" = 14
mkdir /tmp/sq2-sf-trial.lock
token=$(date +%s).$$
state=/tmp/sq2-sf-trial.$token
stage=/data/local/tmp/sq2-closed/sf-trial.$token
mkdir "$state"
cat /proc/sys/kernel/random/boot_id > "$state/boot"
printf '%s\n' "$stage" > "$state/stage"
mkdir -p "$root$stage/egl"
cp "$root$base/"*.so "$root$stage/egl/"
cp "$root$base/SHA256SUMS" "$root$stage/egl/"
(cd "$root$stage/egl";sha256sum -c SHA256SUMS)
chmod 755 "$root$stage" "$root$stage/egl"
chmod 644 "$root$stage/egl/"*.so
ln -s libquest2-egl-wrapper.so "$root$stage/egl/libEGL_freedreno.so"
ln -s libGLESv1_CM_adreno.so "$root$stage/egl/libGLESv1_CM_freedreno.so"
ln -s libGLESv2_adreno.so "$root$stage/egl/libGLESv2_freedreno.so"
cp "$0" "$state/trial.sh"
nohup sh "$state/trial.sh" --watchdog "$state" > "$state/watchdog.log" 2>&1 < /dev/null &
watchdog=$!
printf '%s\n' "$watchdog" > "$state/watchdog-pid"
sleep 1
kill -0 "$watchdog"
tr '\000' ' ' < "/proc/$watchdog/cmdline" | grep -F -- "$state/trial.sh --watchdog $state"
trap 'rollback "$state"' HUP INT TERM
android /system/bin/setprop ctl.stop surfaceflinger
sleep 2
if android /system/bin/pidof surfaceflinger;then rollback "$state";exit 1;fi
if ! android /system/bin/mount --bind "$stage/egl" /vendor/lib64/egl;then rollback "$state";exit 1;fi
if ! android /system/bin/mount --bind "$stage/egl/libquest2-vulkan-wrapper.so" /vendor/lib64/hw/vulkan.freedreno.so;then rollback "$state";exit 1;fi
same_inode "$root/vendor/lib64/egl" "$root$stage/egl"
same_inode "$root/vendor/lib64/hw/vulkan.freedreno.so" "$root$stage/egl/libquest2-vulkan-wrapper.so"
grep -E ' /vendor/lib64/(egl|hw/vulkan.freedreno.so) ' /proc/1/mountinfo > "$state/trial-mounts"
android /system/bin/setprop ctl.start surfaceflinger
printf 'SF_TRIAL_STATE=%s\nSF_TRIAL_STAGE=%s\nSF_TRIAL_WATCHDOG=%s\nSF_TRIAL_ROLLBACK_SECONDS=60\n' "$state" "$stage" "$watchdog"
