#!/bin/busybox sh
# Observe existing traffic for one second; submits no decoder commands.
set -eu
trace=/proc/1/root/sys/kernel/tracing
instance=$trace/instances/sq2_hfi_events
output=/proc/1/root/data/local/tmp/sq2-hfi-existing-events.txt
test ! -e "$output"
test "$(cat /sys/module/suspend/parameters/screen_only)" = Y
test ! -e "$trace/events/sq2_hfi_events"
mkdir "$instance"
registered=0
cleanup() {
 echo 0 > "$instance/tracing_on"
 if [ "$registered" = 1 ]; then
  echo 0 > "$instance/events/sq2_hfi_events/packet/enable"
  cat "$instance/trace" > "$output"
  echo '-:sq2_hfi_events/packet' >> "$trace/kprobe_events"
 fi
 rmdir "$instance"
 sync
}
trap cleanup EXIT
trap 'exit 130' INT TERM HUP
echo 0 > "$instance/tracing_on"
echo 64 > "$instance/buffer_size_kb"
echo 'p:sq2_hfi_events/packet venus_core:hfi_process_msg_packet size=+0(%x1):u32 type=+4(%x1):x32 session=+8(%x1):x32 event=+12(%x1):x32 data1=+16(%x1):x32 data2=+20(%x1):x32' >> "$trace/kprobe_events"
registered=1
echo 'type == 0x21001 && size >= 24' > "$instance/events/sq2_hfi_events/packet/filter"
echo 1 > "$instance/events/sq2_hfi_events/packet/enable"
echo 1 > "$instance/tracing_on"
sleep 1
