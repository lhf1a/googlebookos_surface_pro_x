#!/system/bin/sh
# Bounded real-app observation; never changes app data or private preferences.
set -eu
label=${1:-discovery}
case "$label" in discovery|alias|quality|quality1080|qcom) ;; *) exit 2;; esac
duration=15
case "$label" in quality*|qcom) duration=90;; esac
base=/data/local/tmp/sq2-bilibili-$label
mkdir "$base"
target=/vendor/etc/media_codecs_c2.xml
test ! -e "$target"
test "$(cat /sys/module/suspend/parameters/screen_only)" = Y
test "$(getprop init.svc.sq2-c2-experimental)" = running
user=$(am get-current-user)
logger=
cleanup() {
 am force-stop --user "$user" tv.danmaku.bilibilihd
 if [ -n "$logger" ]; then kill "$logger" 2>/dev/null || true; wait "$logger" 2>/dev/null || true; fi
 rm -f "$target"
 setprop ctl.restart media
 sleep 2
 echo RESTORED
 sync
}
trap cleanup EXIT
trap 'exit 130' INT TERM HUP
cp /data/local/tmp/sq2-media-codecs-c2.xml "$target"
chmod 644 "$target"
restorecon "$target"
setprop ctl.restart media
sleep 2
am force-stop --user "$user" tv.danmaku.bilibilihd
timeout -k 3 120 logcat -b all -T 1 -v threadtime -f "$base/logcat.txt" -r 4096 -n 2 '*:I' &
logger=$!
am start -W --user "$user" -a android.intent.action.VIEW -d https://www.bilibili.com/video/BV1NGZtBwELa -p tv.danmaku.bilibilihd
sleep "$duration"
pidof tv.danmaku.bilibilihd > "$base/app-pid.txt"
dumpsys media.resource_manager > "$base/resources.txt"
screencap -p "$base/frame.png"
echo OBSERVED
