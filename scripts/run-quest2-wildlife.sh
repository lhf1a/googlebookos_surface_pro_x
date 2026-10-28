#!/bin/sh
# Retained initramfs SSH root. Temporary RAM Vulkan selector, exact 3DMark only.
# Stage runtime + selector under /data/local/tmp/sq2-closed/wildlife beforehand.
set -eu
pkg=com.futuremark.dmandroid.application
base=/data/local/tmp/sq2-closed/wildlife
target=/vendor/lib64/hw/vulkan.freedreno.so
fallback=/vendor/lib64/hw/vulkan.sq2-mesa-baseline.so
vendor=/vendor/lib64/sq2-quest2
root=/proc/1/root
expected=0b36eeba9d77ed15e7465396314df80a9479f0b5ccd858d362b40af39fb1759c
a() { exe=$1;shift;nsenter -t 1 -m -r -- "/system/bin/$exe" "$@"; }
hash() { sha256sum "$1" | cut -d ' ' -f1; }
test "$(cat /sys/module/suspend/parameters/screen_only)" = Y
test "$(hash "$root$target")" = "$expected"
test -f "$root$base/vulkan.sq2-selector.so"
(cd "$root$base/runtime";sha256sum -c SHA256SUMS)
sensor=
for z in /sys/class/thermal/thermal_zone*;do
    case "$(cat "$z/type")" in gpu-thermal-top) sensor=$z/temp;; esac
done
test -n "$sensor"
mkdir /tmp/sq2-quest2-wildlife.lock
echo $$ >/tmp/sq2-quest2-wildlife.lock/pid
user=$(a am get-current-user)
boot=$(cat /proc/sys/kernel/random/boot_id)
pids=$(a pidof system_server surfaceflinger)
mounted=0
cleanup() {
    a am force-stop --user "$user" "$pkg" || true
    # Mapped HALs can keep this temporary bind busy. Detach only this known
    # file mount; verify the underlying Mesa hash before reporting restoration.
    if [ "$mounted" = 1 ];then a umount "$target" || a umount -l "$target" || return 1;fi
    test "$(hash "$root$target")" = "$expected" || return 1
    rm -f /tmp/sq2-quest2-wildlife.lock/pid /tmp/sq2-quest2-wildlife.lock/stop
    rmdir /tmp/sq2-quest2-wildlife.lock
    echo MESA_RESTORED
}
trap cleanup EXIT
trap 'exit 130' INT TERM HUP
# /vendor is the existing RAM overlay. No ESP, disk image or APK changes.
if [ -e "$root$fallback" ];then test "$(hash "$root$fallback")" = "$expected";else cp "$root$target" "$root$fallback";fi
mkdir -p "$root$vendor"
cp "$root$base/runtime/"*.so "$root$vendor/"
chmod 644 "$root$vendor/"*.so "$root$fallback"
a am force-stop --user "$user" "$pkg"
a mount --bind "$base/vulkan.sq2-selector.so" "$target"
mounted=1
echo SELECTOR_ACTIVE
a am start --user "$user" -W -n "$pkg/com.futuremark.flamenco.ui.splash.SplashPageActivity"
tick=0
while [ "$tick" -lt 150 ];do
    test ! -e /tmp/sq2-quest2-wildlife.lock/stop || break
    test "$boot" = "$(cat /proc/sys/kernel/random/boot_id)"
    test "$pids" = "$(a pidof system_server surfaceflinger)"
    temp=$(cat "$sensor");echo "GPU_MILLIC=$temp"
    test "$temp" -lt 80000
    sleep 2;tick=$((tick+1))
done
