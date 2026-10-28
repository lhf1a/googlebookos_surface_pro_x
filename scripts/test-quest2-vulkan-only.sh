#!/bin/sh
# Finite system Vulkan trial. Mesa EGL/GLES bytes are preserved; no boot writes.
set -eu
umask 077
root=/proc/1/root
base=/data/local/tmp/sq2-closed/vulkan-only-counter-v1
mesa_gl=f16302e33e4f7c8bbaccbe5ab5fd5d19f3279222ee430ace236c4bad237a5434
mesa_vk=0b36eeba9d77ed15e7465396314df80a9479f0b5ccd858d362b40af39fb1759c
candidate_vk=bcb9b506dc2a4b1c4a53e85c08057d7cb5a0e6e3bd2bd97a90b92de20eb76d42
android(){ nsenter -t 1 -m -r -- "$@"; }
hash(){ sha256sum "$1" | cut -d ' ' -f1; }
same_inode(){ test "$(stat -c '%d:%i' "$1")" = "$(stat -c '%d:%i' "$2")"; }
check_default(){
    test "$(hash "$root/vendor/lib64/egl/libEGL_freedreno.so")" = "$mesa_gl"
    test "$(hash "$root/vendor/lib64/hw/vulkan.freedreno.so")" = "$mesa_vk"
}
state_check(){
    state=$1
    case "$state" in /tmp/sq2-vk-trial.[0-9]*.[0-9]*);; *) exit 2;;esac
    test "$(cat "$state/boot")" = "$(cat /proc/sys/kernel/random/boot_id)"
    stage=$(cat "$state/stage")
    case "$stage" in /data/local/tmp/sq2-closed/vk-trial.[0-9]*.[0-9]*);; *) exit 2;;esac
}
rollback(){
    state_check "$1"
    if ! mkdir "$state/rolling-back" 2>/dev/null;then return;fi
    echo VULKAN_TRIAL_ROLLBACK_BEGIN
    if same_inode "$root/vendor/lib64/hw/vulkan.freedreno.so" "$root$stage/egl/libquest2-vulkan-wrapper.so";then
        android /system/bin/umount /vendor/lib64/hw/vulkan.freedreno.so || android /system/bin/umount -l /vendor/lib64/hw/vulkan.freedreno.so
    fi
    if same_inode "$root/vendor/lib64/egl" "$root$stage/egl";then
        android /system/bin/umount /vendor/lib64/egl || android /system/bin/umount -l /vendor/lib64/egl
    fi
    check_default
    # Drop candidate mappings in application processes. SF remains on Mesa.
    android /system/bin/setprop ctl.restart zygote
    touch "$state/complete"
    rmdir /tmp/sq2-quest2-gles.lock
    echo VULKAN_TRIAL_DEFAULT_MESA_RESTORED
}
case "${1:-}" in
    --rollback) test "$#" = 2;rollback "$2";exit;;
    --watchdog)
        test "$#" = 2;state_check "$2"
        touch "$state/ready"
        sleep 120
        rollback "$state"
        exit;;
    --screenshots)
        test "$#" = 2;state_check "$2"
        for i in 1 2 3 4 5 6;do
            sleep 12
            test ! -d "$state/rolling-back" || exit 0
            timeout -k 2 8 nsenter -t 1 -m -r -- /system/bin/screencap -p "$stage/evidence/screen.$i.png" || true
            android /system/bin/ps -A > "$root$stage/evidence/ps.$i"
            for p in $(android /system/bin/pidof surfaceflinger com.android.systemui com.android.chrome org.surface.sq2.vulkanprobe || true);do
                grep -E 'vulkan|lib(EGL|GLES|quest2|gsl|q2v77)' "/proc/$p/maps" > "$root$stage/evidence/maps.$i.$p" 2>/dev/null || true
            done
        done
        exit;;
    --start) test "$#" = 1;;
    *) exit 2;;
esac
test "$(id -u)" = 0
test "$(stat -c '%d:%i' /)" != "$(stat -c '%d:%i' "$root")"
test "$(cat /sys/module/suspend/parameters/screen_only)" = Y
test "$(android /system/bin/getprop sys.boot_completed)" = 1
check_default
test "$(hash "$root$base/vulkan-v77-bridge.so")" = "$candidate_vk"
(cd "$root$base";sha256sum -c SHA256SUMS)
test "$(find "$root$base" -maxdepth 1 -name '*.so' | wc -l)" = 9
test ! -e "$root$base/libquest2-egl-wrapper.so"
test ! -d /tmp/sq2-sf-trial.lock
mkdir /tmp/sq2-quest2-gles.lock
token=$(date +%s).$$
state=/tmp/sq2-vk-trial.$token
stage=/data/local/tmp/sq2-closed/vk-trial.$token
mkdir "$state"
cat /proc/sys/kernel/random/boot_id > "$state/boot"
printf '%s\n' "$stage" > "$state/stage"
mkdir -p "$root$stage/egl" "$root$stage/evidence"
cp "$root/vendor/lib64/egl/"*.so "$root$stage/egl/"
for file in "$root/vendor/lib64/egl/"*.so;do
    test "$(hash "$file")" = "$(hash "$root$stage/egl/${file##*/}")"
done
cp "$root$base/"*.so "$root$stage/egl/"
test "$(hash "$root$stage/egl/libEGL_freedreno.so")" = "$mesa_gl"
test ! -e "$root$stage/egl/libGLESv2_adreno.so"
chmod 755 "$root$stage" "$root$stage/egl"
chmod 644 "$root$stage/egl/"*.so
cp "$0" "$state/trial.sh"
nohup sh "$state/trial.sh" --watchdog "$state" > "$state/watchdog.log" 2>&1 < /dev/null &
watchdog=$!
sleep 1
kill -0 "$watchdog"
test -f "$state/ready"
trap 'rollback "$state"' HUP INT TERM
if ! android /system/bin/mount --bind "$stage/egl" /vendor/lib64/egl;then rollback "$state";exit 1;fi
if ! android /system/bin/mount --bind "$stage/egl/libquest2-vulkan-wrapper.so" /vendor/lib64/hw/vulkan.freedreno.so;then rollback "$state";exit 1;fi
same_inode "$root/vendor/lib64/egl" "$root$stage/egl"
same_inode "$root/vendor/lib64/hw/vulkan.freedreno.so" "$root$stage/egl/libquest2-vulkan-wrapper.so"
nohup sh "$state/trial.sh" --screenshots "$state" > "$root$stage/evidence/screenshots.log" 2>&1 < /dev/null &
printf 'VULKAN_TRIAL_STATE=%s\nVULKAN_TRIAL_STAGE=%s\nROLLBACK_SECONDS=120\n' "$state" "$stage"
