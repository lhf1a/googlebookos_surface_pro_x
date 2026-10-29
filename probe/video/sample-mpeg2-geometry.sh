#!/bin/busybox sh
set -eu
trace=/proc/1/root/sys/kernel/tracing
instance=$trace/instances/sq2_mpeg2_geometry
output=/proc/1/root/data/local/tmp/sq2-mpeg2-geometry-trace.txt
test ! -e "$output"
test "$(cat /sys/module/suspend/parameters/screen_only)" = Y
test ! -e "$trace/events/sq2_mpeg2_geometry"
mkdir "$instance"
registered=0
cleanup() {
 echo 0 > "$instance/tracing_on"
 if [ "$registered" = 1 ]; then
  echo 0 > "$instance/events/sq2_mpeg2_geometry/change/enable"
  cat "$instance/trace" > "$output"
  echo '-:sq2_mpeg2_geometry/change' >> "$trace/kprobe_events"
 fi
 rmdir "$instance"
 sync
}
trap cleanup EXIT
trap 'exit 130' INT TERM HUP
echo 0 > "$instance/tracing_on"
echo 128 > "$instance/buffer_size_kb"
# Fixed hfi_event_data: u32 fields, crop begins at byte 56.
echo 'p:sq2_mpeg2_geometry/change venus_dec:vdec_event_change error=+0(%x1):x32 height=+4(%x1):u32 width=+8(%x1):u32 depth=+36(%x1):u32 picture=+40(%x1):u32 left=+56(%x1):u32 top=+60(%x1):u32 cropwidth=+64(%x1):u32 cropheight=+68(%x1):u32' >> "$trace/kprobe_events"
registered=1
echo 1 > "$instance/events/sq2_mpeg2_geometry/change/enable"
echo 1 > "$instance/tracing_on"
set +e
timeout -k 3 25 /tmp/mpeg2-decode-test --dmabuf /dev/video0 > /proc/1/root/data/local/tmp/sq2-mpeg2-geometry-decode.txt 2>&1
result=$?
set -e
echo DECODE_EXIT=$result
