#!/bin/sh
# Temporary mount namespace only. Android EGL/GLES dispatches the private donor;
# PID 1, SurfaceFlinger, on-disk vendor and the boot image retain Mesa.
set -eu
base=/data/local/tmp/sq2-closed/gles-linked
system_probe=/data/local/tmp/sq2-closed/gles-v77
if [ "${1:-}" = --inside ];then
    shift
    test "$(readlink /proc/self/ns/mnt)" != "$(readlink /proc/1/ns/mnt)"
    # The checked launcher made / recursively private, then mounted this tmpfs.
    test "$(stat -f -c %T /vendor/lib64/egl)" = tmpfs
    cp "$base/"*.so /vendor/lib64/egl/
    ln -s libquest2-egl-wrapper.so /vendor/lib64/egl/libEGL_freedreno.so
    ln -s libGLESv1_CM_adreno.so /vendor/lib64/egl/libGLESv1_CM_freedreno.so
    ln -s libGLESv2_adreno.so /vendor/lib64/egl/libGLESv2_freedreno.so
    echo "PRIVATE_EGL_NAMESPACE=$(readlink /proc/self/ns/mnt)"
    exec env -u LD_PRELOAD -u LD_LIBRARY_PATH "$system_probe/gles-readback-system" "$@"
fi
case "${1:-}" in ''|--ahb) ;; *) echo 'usage: run-quest2-gles-system.sh [--ahb]' >&2;exit 2;; esac
test "$#" -le 1
test "$(cat /sys/module/suspend/parameters/screen_only)" = Y
root=/proc/1/root
expected=f16302e33e4f7c8bbaccbe5ab5fd5d19f3279222ee430ace236c4bad237a5434
check_mesa(){ test "$(sha256sum "$root/vendor/lib64/egl/libEGL_freedreno.so" | cut -d ' ' -f1)" = "$expected"; }
check_mesa
test "$(nsenter -t 1 -m -r -- /system/bin/getprop ro.hardware.egl)" = freedreno
(cd "$root$base";sha256sum -c SHA256SUMS;sha256sum -c PROBE_SHA256SUMS)
(cd "$root$system_probe";sha256sum -c SYSTEM_PROBE_SHA256SUMS)
mkdir /tmp/sq2-quest2-gles.lock
trap 'rmdir /tmp/sq2-quest2-gles.lock' EXIT
boot=$(cat /proc/sys/kernel/random/boot_id)
echo "BOOT_BEFORE=$boot"
nsenter -t 1 -m -r -- /system/bin/pidof system_server surfaceflinger
result=0
timeout -k 3 25 nsenter -t 1 -m -r -- "$system_probe/egl-private-namespace" "$@" || result=$?
echo "SYSTEM_PROBE_EXIT=$result"
test "$(cat /proc/sys/kernel/random/boot_id)" = "$boot"
check_mesa
echo DEFAULT_MESA_UNCHANGED
nsenter -t 1 -m -r -- /system/bin/pidof system_server surfaceflinger
exit "$result"
