#!/bin/sh
# Private Vulkan replacement only; no persistent/global mounts or EGL changes.
set -eu
home=/data/local/tmp/sq2-closed
candidate=$home/vulkan-only-v1
case "${SQ2_VK_COUNTER_CLEANUP:-0}" in
    0) ;;
    1) candidate=$home/vulkan-only-counter-v1;;
    *) exit 2;;
esac
probe=$home/vulkan-device
expected_egl=f16302e33e4f7c8bbaccbe5ab5fd5d19f3279222ee430ace236c4bad237a5434
expected_vk=0b36eeba9d77ed15e7465396314df80a9479f0b5ccd858d362b40af39fb1759c
if [ "${1:-}" = --inside ];then
    shift
    test "$(readlink /proc/self/ns/mnt)" != "$(readlink /proc/1/ns/mnt)"
    test "$(stat -f -c %T /vendor/lib64/egl)" = tmpfs
    test "$(sha256sum /proc/self/fd/9/libEGL_freedreno.so | cut -d ' ' -f1)" = "$expected_egl"
    cp /proc/self/fd/9/*.so /vendor/lib64/egl/
    for original in /proc/self/fd/9/*.so;do
        test "$(sha256sum "$original" | cut -d ' ' -f1)" = "$(sha256sum "/vendor/lib64/egl/${original##*/}" | cut -d ' ' -f1)"
    done
    exec 9<&-
    # This bundle deliberately excludes all donor EGL/GLES entry points.
    for library in "$candidate/"*.so;do
        test ! -e "/vendor/lib64/egl/${library##*/}"
        cp "$library" /vendor/lib64/egl/
    done
    test ! -e /vendor/lib64/egl/libGLESv2_adreno.so
    test ! -e /vendor/lib64/egl/libquest2-egl-wrapper.so
    test "$(sha256sum /vendor/lib64/egl/libEGL_freedreno.so | cut -d ' ' -f1)" = "$expected_egl"
    mount --bind /vendor/lib64/egl/libquest2-vulkan-wrapper.so /vendor/lib64/hw/vulkan.freedreno.so
    echo VULKAN_ONLY_PRIVATE_MESA_EGL_PRESERVED
    case "$1" in
        --device) exec env -u LD_PRELOAD -u LD_LIBRARY_PATH "$probe/quest2-vulkan-device";;
        --offscreen) exec env -u LD_PRELOAD -u LD_LIBRARY_PATH SQ2_VULKAN_REJECT_TURNIP=1 "$home/v77-vulkan-probe/vulkan-readback" /system/lib64/libvulkan.so --offscreen-only;;
        --ahb) exec env -u LD_PRELOAD -u LD_LIBRARY_PATH SQ2_VULKAN_REJECT_TURNIP=1 "$home/v77-vulkan-probe/vulkan-readback" /system/lib64/libvulkan.so --ahb-cycles "${SQ2_VULKAN_AHB_CYCLES:-1}";;
        --window) exec env -u LD_PRELOAD -u LD_LIBRARY_PATH SQ2_VULKAN_REJECT_TURNIP=1 "$home/vulkan-window/quest2-vulkan-window";;
    esac
    exit 2
fi
test "$#" = 1
case "$1" in --device|--offscreen|--ahb|--window);; *) exit 2;;esac
root=/proc/1/root
check_default(){
    test "$(sha256sum "$root/vendor/lib64/egl/libEGL_freedreno.so" | cut -d ' ' -f1)" = "$expected_egl"
    test "$(sha256sum "$root/vendor/lib64/hw/vulkan.freedreno.so" | cut -d ' ' -f1)" = "$expected_vk"
}
check_default
test "$(cat /sys/module/suspend/parameters/screen_only)" = Y
(cd "$root$candidate"; sha256sum -c SHA256SUMS)
(cd "$root$probe"; sha256sum -c VULKAN_DEVICE_SHA256SUMS)
case "$1" in
    --offscreen|--ahb) (cd "$root$home/v77-vulkan-probe"; sha256sum -c VULKAN_PROBE_SHA256SUMS);;
    --window) (cd "$root$home/vulkan-window"; sha256sum -c VULKAN_WINDOW_SHA256SUMS);;
esac
test ! -d /tmp/sq2-sf-trial.lock
mkdir /tmp/sq2-quest2-gles.lock
trap 'rmdir /tmp/sq2-quest2-gles.lock' EXIT
boot=$(cat /proc/sys/kernel/random/boot_id)
pids=$(nsenter -t 1 -m -r -- /system/bin/pidof system_server surfaceflinger)
result=0
timeout -k 3 25 nsenter -t 1 -m -r -- "$probe/quest2-vulkan-namespace" "$1" || result=$?
echo "VULKAN_ONLY_PROBE_EXIT=$result"
test "$(cat /proc/sys/kernel/random/boot_id)" = "$boot"
test "$(nsenter -t 1 -m -r -- /system/bin/pidof system_server surfaceflinger)" = "$pids"
check_default
echo DEFAULT_MESA_BOOT_FRAMEWORK_UNCHANGED
exit "$result"
