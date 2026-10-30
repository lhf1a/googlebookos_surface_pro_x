#!/system/bin/sh
# Scoped laboratory advertisement, ordinary-UID SurfaceView playback.
set -eu
target=/vendor/etc/media_codecs_c2.xml
test ! -e "$target"
test "$(cat /sys/module/suspend/parameters/screen_only)" = Y
test "$(getprop init.svc.sq2-c2-experimental)" = running
test -c /dev/video0
boot=$(cat /proc/sys/kernel/random/boot_id)
label=${1:-paced}
case "$label" in paced|720p|1080p|1080p-lifecycle|1080p-close|1080p-stack|1080p-stack2|1080p-copy|2160p|2160p-timing|2160p-neon|2160p-vu|2160p-vector|2160p-pool|2160p-owner|2160p-map-cache|2160p-import|2160p-seek|2160p-seek2|2160p-long|2160p-auto|2160p-auto2|2160p-auto3|2160p-auto4|2160p-auto5|2160p-auto-high|2160p-auto-high2|2160p-auto-high3|1080p-main|1080p-high|2160p-high|2160p-high2|hevc-small|hevc-main10-small|hevc-4k60|hevc-4k60b|hevc-4k60c|vp8-small|vp8-small2|vp8-small3|vp8-4k|vp9-small|vp9-small2|vp9-4k|vp9-4k60) ;; *) exit 2;; esac
evidence=/data/local/tmp/sq2-player-$label-$boot
mkdir "$evidence"
user=$(am get-current-user)
case "$user" in ''|*[!0-9]*) exit 2;; esac
result=/data/user/$user/org.surface.sq2.videotest/files/result.txt
logger=
refresh() {
 setprop ctl.restart media
 for n in 1 2 3 4 5 6 7 8 9 10; do
  sleep 1
  if [ "$(getprop init.svc.media)" = running ]; then return 0; fi
 done
 return 1
}
cleanup() {
 logcat -b all -d -v threadtime SQ2C2Lifecycle:I CCodec:E MediaCodec:E SQ2VideoTest:I '*:S' > "$evidence/final-logcat.txt"
 am force-stop --user "$user" org.surface.sq2.videotest
 if [ -n "$logger" ]; then kill "$logger" 2>/dev/null || true; wait "$logger" 2>/dev/null || true; fi
 rm -f "$target"
 refresh
 echo RESTORED
 sync
}
trap cleanup EXIT
trap 'exit 130' INT TERM HUP
timeout -k 3 90 logcat -b all -T 1 -v threadtime -f "$evidence/logcat.txt" -r 1024 -n 1 SQ2C2Lifecycle:I CCodec:V Codec2Client:V MediaCodec:V SQ2VideoTest:I '*:S' &
logger=$!
cp /data/local/tmp/sq2-media-codecs-c2.xml "$target"
chmod 644 "$target"
restorecon "$target"
echo XML_READY
sync
refresh
am force-stop --user "$user" org.surface.sq2.videotest
seek=false
loops=1
if [ "$label" = 2160p-long ]; then loops=6; fi
case "$label" in 2160p-seek*) seek=true;; esac
probe_profile=false
case "$label" in 1080p-main|1080p-high|2160p-high|2160p-high2) probe_profile=true;; esac
codec=c2.sq2.avc.experimental
case "$label" in 2160p-auto*|hevc-*|vp8-*|vp9-*) codec=auto;; esac
am start -W --user "$user" -n org.surface.sq2.videotest/.MainActivity --es codec "$codec" --ez seek "$seek" --ei loops "$loops" --ez probe-profile "$probe_profile"
sleep 3
screencap -p "$evidence/frame-early.png"
sleep 3
screencap -p "$evidence/frame-late.png"
n=0
while [ "$n" -lt 80 ]; do
 n=$((n+1))
 if grep -qE 'RELEASE_OK|RELEASE_ERROR|RELEASE_AFTER_STOP_ERROR|^ERROR ' "$result"; then break; fi
 sleep 1
done
cp "$result" "$evidence/result.txt"
cat "$evidence/result.txt"
sync
if [ "$seek" = true ]; then
 grep -q 'FLUSH_OK target_us=4933333 expected_outputs=212' "$evidence/result.txt"
 grep -qE 'RESULT pass=true inputs=[0-9]+ outputs=212 rendered=212 eos=true cancelled=false realtime=true' "$evidence/result.txt"
elif [ "$label" = vp9-4k60 ] || [ "$label" = hevc-4k60 ] || [ "$label" = hevc-4k60b ] || [ "$label" = hevc-4k60c ]; then
 grep -q 'RESULT pass=true inputs=600 outputs=600 rendered=600 eos=true cancelled=false realtime=true' "$evidence/result.txt"
elif [ "$loops" = 6 ]; then
 grep -q 'RESULT pass=true inputs=1800 outputs=1800 rendered=1800 eos=true cancelled=false realtime=true' "$evidence/result.txt"
else
 grep -q 'RESULT pass=true inputs=300 outputs=300 rendered=300 eos=true cancelled=false' "$evidence/result.txt"
fi
grep -q RELEASE_OK "$evidence/result.txt"
