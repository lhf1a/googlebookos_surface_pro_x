#!/bin/sh
# System Vulkan trial. Mesa EGL/GLES bytes are preserved; no boot writes.
# Default: 120-second rollback. Explicit --start-manual keeps the candidate
# until --rollback STATE; the operator must retain the printed state path.
set -eu
umask 077
root=/proc/1/root
base=/data/local/tmp/sq2-closed/vulkan-only-counter-v1
expected_wrapper=1cf8564084cbce59937ba512cd58f26a44bd148c80d306336b1fcb4db1e0ebd7
case "${SQ2_VK_APP_DIAGNOSTICS:-0}" in
    0) ;;
    1) base=/data/local/tmp/sq2-closed/vulkan-app-diag-v1; expected_wrapper=6e8bec46906af85be37fc0c4895a3b9ce22ee2b5cdda618b418701e2b50b95f8;;
    2) base=/data/local/tmp/sq2-closed/vulkan-app-diag-v2; expected_wrapper=f167c19ee43f62afad20b686508e449b6605dd9e008c8202474c22b309648c7d;;
    3) base=/data/local/tmp/sq2-closed/vulkan-app-diag-v3; expected_wrapper=02c053b282a76287ca374e07ebe1c02c03ca4563d10c47458b1a0018043780aa;;
    4) base=/data/local/tmp/sq2-closed/vulkan-app-diag-v4; expected_wrapper=ee2d0aba6adb399e2a12dec72ea55d664e5a549ade2e31c45c6892ed938999a3;;
    5) base=/data/local/tmp/sq2-closed/vulkan-app-diag-v5; expected_wrapper=db92112e775733180c8e803d54f4a12659911f7927c6d721c70395cc8d83d885;;
    *) exit 2;;
esac
case "${SQ2_VK_RUNTIME:-0}" in
    0) ;;
    3|5|7)
        test "${SQ2_VK_APP_DIAGNOSTICS:-0}" = 0
        base=/data/local/tmp/sq2-closed/vulkan-runtime-v$SQ2_VK_RUNTIME
        expected_wrapper=69b710019872e1d622e797a35f8d4f636e9a0439028d0e6815ac0c70fa389b7f
        if [ "$SQ2_VK_RUNTIME" = 5 ];then
            expected_wrapper=4cfe5cba0496e9b007344a53725aa2595cdfa0d777f880f9d468a065b36c78a0
        fi
        if [ "$SQ2_VK_RUNTIME" = 7 ];then
            expected_wrapper=2c6c1d0ed676c13ebb8aae47c9020a9b66fcd25faca8752064082cf56e587fa1
        fi
        ;;
    *) exit 2;;
esac
mesa_gl=f16302e33e4f7c8bbaccbe5ab5fd5d19f3279222ee430ace236c4bad237a5434
mesa_vk=0b36eeba9d77ed15e7465396314df80a9479f0b5ccd858d362b40af39fb1759c
version_xml=/vendor/etc/permissions/android.hardware.vulkan.version-1_3.xml
mesa_version_hash=bb312d894dc216dae00b7258a5e015b680186f117e5066e8925801247c5d70f6
candidate_vk=bcb9b506dc2a4b1c4a53e85c08057d7cb5a0e6e3bd2bd97a90b92de20eb76d42
if [ "${SQ2_VK_RUNTIME:-0}" = 5 ] || [ "${SQ2_VK_RUNTIME:-0}" = 7 ];then
    candidate_vk=c6253f16ae790a5f845c563e2705e36a005aecf327ed312f7de4ddcbd0e60dce
fi
android(){ nsenter -t 1 -m -r -- "$@"; }
hash(){ sha256sum "$1" | cut -d ' ' -f1; }
same_inode(){ test "$(stat -c '%d:%i' "$1")" = "$(stat -c '%d:%i' "$2")"; }
check_default(){
    test "$(hash "$root/vendor/lib64/egl/libEGL_freedreno.so")" = "$mesa_gl"
    test "$(hash "$root/vendor/lib64/hw/vulkan.freedreno.so")" = "$mesa_vk"
    test "$(hash "$root$version_xml")" = "$mesa_version_hash"
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
    if [ -f "$state/direct" ] && same_inode "$root/data/sq2vk-v77t" "$root$stage/direct";then
        android /system/bin/umount /data/sq2vk-v77t || android /system/bin/umount -l /data/sq2vk-v77t
    fi
    if [ -f "$state/version-xml" ] && same_inode "$root$version_xml" "$root$stage/vulkan-version.xml";then
        android /system/bin/umount "$version_xml" || android /system/bin/umount -l "$version_xml"
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
    --start) test "$#" = 1; manual=0;;
    --start-manual) test "$#" = 1; manual=1;;
    *) exit 2;;
esac
test "$(id -u)" = 0
test "$(stat -c '%d:%i' /)" != "$(stat -c '%d:%i' "$root")"
test "$(cat /sys/module/suspend/parameters/screen_only)" = Y
test "$(android /system/bin/getprop sys.boot_completed)" = 1
check_default
test "$(hash "$root$base/vulkan-v77-bridge.so")" = "$candidate_vk"
test "$(hash "$root$base/libquest2-vulkan-wrapper.so")" = "$expected_wrapper"
(cd "$root$base";sha256sum -c SHA256SUMS)
test "$(find "$root$base" -maxdepth 1 -name '*.so' | wc -l)" = 9
test ! -e "$root$base/libquest2-egl-wrapper.so"
if [ "${SQ2_VK_RUNTIME:-0}" != 0 ];then
    if [ "$SQ2_VK_RUNTIME" = 3 ];then
        test "$(hash "$root$base/libquest2-v77-ahb.so")" = 050cadeb3958c4cd5ca148285186e3a23fa6ad5e59e35ca0faf56f246fc4bd91
    else
        expected_ahb=3f758905767cb150aea77dee68c300b55c39701394edbf9a899ad770511ba3f2
        if [ "$SQ2_VK_RUNTIME" = 7 ];then
            expected_ahb=7af3af6965eb9b85fffc4ad54995b2d3bafb6efe7e38197b4b2b1f45a4feefd8
        fi
        test "$(hash "$root$base/libquest2-v77-ahb.so")" = "$expected_ahb"
        test "$(hash "$root$base/libgsl.so")" = b302ae0d5a6041be816ebe07c0c8c2686baf231b54182ee319731512db35523d
    fi
    test "$(hash "$root$base/qgl_config.txt")" = 1f93a7a1446b2e934f12f23aa55514ee41852bf6df7851788d053cbd90e1f008
    test -d "$root/data/sq2vk-v77t"
    test ! -e "$root/data/sq2vk-v77t/qgl_config.txt"
fi
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
if [ "$manual" = 0 ];then
nohup sh "$state/trial.sh" --watchdog "$state" > "$state/watchdog.log" 2>&1 < /dev/null &
watchdog=$!
sleep 1
kill -0 "$watchdog"
test -f "$state/ready"
fi
trap 'rollback "$state"' HUP INT TERM
if [ "${SQ2_VK_RUNTIME:-0}" = 7 ];then
    # The old Mesa 1.3 declaration would override the existing 1.1 XML.
    # Change only this lab overlay; no fabricated dEQP certification date.
    printf '%s\n' '<permissions><feature name="android.hardware.vulkan.version" version="4198400"/></permissions>' > "$root$stage/vulkan-version.xml"
    chmod 644 "$root$stage/vulkan-version.xml"
    touch "$state/version-xml"
    if ! android /system/bin/mount --bind "$stage/vulkan-version.xml" "$version_xml";then rollback "$state";exit 1;fi
fi
if [ "${SQ2_VK_RUNTIME:-0}" != 0 ];then
    mkdir "$root$stage/direct"
    cp "$root$base/qgl_config.txt" "$root$stage/direct/"
    chmod 755 "$root$stage/direct"
    chmod 644 "$root$stage/direct/qgl_config.txt"
    touch "$state/direct"
    if ! android /system/bin/mount --bind "$stage/direct" /data/sq2vk-v77t;then rollback "$state";exit 1;fi
fi
if ! android /system/bin/mount --bind "$stage/egl" /vendor/lib64/egl;then rollback "$state";exit 1;fi
if ! android /system/bin/mount --bind "$stage/egl/libquest2-vulkan-wrapper.so" /vendor/lib64/hw/vulkan.freedreno.so;then rollback "$state";exit 1;fi
same_inode "$root/vendor/lib64/egl" "$root$stage/egl"
same_inode "$root/vendor/lib64/hw/vulkan.freedreno.so" "$root$stage/egl/libquest2-vulkan-wrapper.so"
if [ -f "$state/version-xml" ];then
    # PackageManager reads feature XML at system_server startup.
    android /system/bin/setprop ctl.restart zygote
fi
nohup sh "$state/trial.sh" --screenshots "$state" > "$root$stage/evidence/screenshots.log" 2>&1 < /dev/null &
printf 'VULKAN_TRIAL_STATE=%s\nVULKAN_TRIAL_STAGE=%s\n' "$state" "$stage"
if [ "$manual" = 1 ];then
    printf 'ROLLBACK_MODE=manual\nROLLBACK_COMMAND=sh %s/trial.sh --rollback %s\n' "$state" "$state"
else
    echo ROLLBACK_SECONDS=120
fi
