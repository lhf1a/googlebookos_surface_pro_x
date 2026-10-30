#!/system/bin/sh
# Temporary XML and media-service cache experiment. No core/firmware restart.
set -eu
stage() { echo "STAGE=$1"; sync; }
stage BEGIN
target=/vendor/etc/media_codecs_c2.xml
test ! -e "$target"
test "$(cat /sys/module/suspend/parameters/screen_only)" = Y
test "$(getprop init.svc.sq2-c2-experimental)" = running
test -c /dev/video0
refresh() {
 stage MEDIA_REFRESH_BEGIN
 setprop ctl.restart media
 count=0
 while [ "$count" -lt 20 ]; do
  sleep 1
  if [ "$(getprop init.svc.media)" = running ] && pidof mediaserver >/dev/null; then stage MEDIA_REFRESH_DONE; return 0; fi
  count=$((count+1))
 done
 return 1
}
cleanup() {
 stage CLEANUP_BEGIN
 rm -f "$target"
 refresh
 echo RESTORED
}
trap cleanup EXIT
trap 'exit 130' INT TERM HUP
cp /data/local/tmp/sq2-media-codecs-c2.xml "$target"
chmod 644 "$target"
restorecon "$target"
stage XML_READY
refresh
echo MEDIA_PID=$(pidof mediaserver)
set +e
if [ "${1:-}" = --frames ]; then
 stage FRAMES_BEGIN
 codec=${3:-c2.sq2.avc.experimental}
 case "$codec" in c2.sq2.avc.experimental|OMX.qcom.video.decoder.avc) ;; *) exit 2;; esac
 timeout -k 3 20 /data/local/tmp/sq2-mediacodec-frames "$codec" ${2:-}
else
 timeout 20 /data/local/tmp/sq2-mediacodec-create c2.sq2.avc.experimental ${1:-}
fi
experiment_exit=$?
echo EXPERIMENT_EXIT=$experiment_exit
timeout 20 /data/local/tmp/sq2-mediacodec-create c2.android.avc.decoder
echo SOFTWARE_EXIT=$?
logcat -d -t 2000 | grep -E 'CCodec|Codec2Client|c2.sq2|Codec2Info|MediaCodec|codeclist' | tail -80
exit "$experiment_exit"
