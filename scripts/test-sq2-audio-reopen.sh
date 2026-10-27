#!/bin/busybox sh
# Run in the retained diagnostic root, after the user pauses Android playback.
# Temporary A/B only: always restore the original delayed-powerdown settings.
set -eu
wav=${1:?usage: test-sq2-audio-reopen.sh absolute-reference.wav}
backend=${2:-alsa}
case "$backend" in alsa|android) ;; *) exit 2;; esac
test -r "$wav"
if test "$backend" = android; then
    case "$wav" in /proc/1/root/*) android_wav=${wav#/proc/1/root};; *) exit 2;; esac
    test -x /proc/1/root/data/local/tmp/sq2-audio-wav-test
fi
grep -qx closed /proc/asound/card0/pcm0p/sub0/status
saved=$(mktemp -d /run/sq2-audio-reopen.XXXXXX)
restore() {
    for name in MultiMedia1 'SLIM Playback' 'SLIM WCD Playback'; do
        if test -f "$saved/$name"; then
            cat "$saved/$name" > "/sys/devices/platform/sound/$name/pmdown_time"
        fi
    done
    echo PMDOWN_BASELINE_RESTORED
}
trap restore EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP
for name in MultiMedia1 'SLIM Playback' 'SLIM WCD Playback'; do
    f="/sys/devices/platform/sound/$name/pmdown_time"
    cat "$f" > "$saved/$name"
done
for name in MultiMedia1 'SLIM Playback' 'SLIM WCD Playback'; do
    echo 0 > "/sys/devices/platform/sound/$name/pmdown_time"
done
for pass in 1 2; do
    echo "REOPEN_PASS=$pass PMDOWN=0 BACKEND=$backend"
    if test "$backend" = android; then
        nsenter -t 1 -m -r -- /data/local/tmp/sq2-audio-wav-test "$android_wav"
    else
        tinyplay "$wav" -D 0 -d 0
    fi
    sleep 10
done
