#!/bin/busybox sh
# Run only after a fresh native initialization and a bounded kernel collector.
# Preserve property submissions preceding the FIRST unsupported-setting event.
set -eu
trace=/proc/1/root/sys/kernel/tracing
instance=$trace/instances/sq2_mpeg2_first
output=/proc/1/root/data/local/tmp/sq2-mpeg2-first-error-trace.txt
test ! -e "$output"
test "$(cat /sys/module/suspend/parameters/screen_only)" = Y
test -c /dev/video0
test -x /tmp/mpeg2-aligned-decode-test
test ! -e "$trace/events/sq2_mpeg2_first"
mkdir "$instance"
event=0
property=0
cleanup() {
 echo 0 > "$instance/tracing_on"
 if [ "$event" = 1 ]; then
  echo 0 > "$instance/events/sq2_mpeg2_first/event/enable"
  echo '!traceoff' > "$instance/events/sq2_mpeg2_first/event/trigger" || true
 fi
 if [ "$property" = 1 ]; then echo 0 > "$instance/events/sq2_mpeg2_first/property/enable"; fi
 cat "$instance/trace" > "$output"
 if [ "$event" = 1 ]; then echo '-:sq2_mpeg2_first/event' >> "$trace/kprobe_events"; fi
 if [ "$property" = 1 ]; then echo '-:sq2_mpeg2_first/property' >> "$trace/kprobe_events"; fi
 rmdir "$instance"
 sync
}
trap cleanup EXIT
trap 'exit 130' INT TERM HUP
echo 0 > "$instance/tracing_on"
echo 128 > "$instance/buffer_size_kb"
echo 'p:sq2_mpeg2_first/property venus_core:hfi_session_set_property property=%x1:x32 word0=+0(%x2):x32' >> "$trace/kprobe_events"
property=1
echo 'p:sq2_mpeg2_first/event venus_core:hfi_process_msg_packet size=+0(%x1):u32 type=+4(%x1):x32 session=+8(%x1):x32 event=+12(%x1):x32 data1=+16(%x1):x32 data2=+20(%x1):x32' >> "$trace/kprobe_events"
event=1
echo 'type == 0x21001 && size >= 24' > "$instance/events/sq2_mpeg2_first/event/filter"
echo 'traceoff:1 if type == 0x21001 && size >= 24 && event == 2 && data1 == 0x1008' > "$instance/events/sq2_mpeg2_first/event/trigger"
echo 1 > "$instance/events/sq2_mpeg2_first/property/enable"
echo 1 > "$instance/events/sq2_mpeg2_first/event/enable"
echo 1 > "$instance/tracing_on"
set +e
timeout -k 3 45 /tmp/mpeg2-aligned-decode-test --dmabuf /dev/video0 > /proc/1/root/data/local/tmp/sq2-mpeg2-first-error-decode.txt 2>&1
result=$?
set -e
echo DECODE_EXIT=$result
