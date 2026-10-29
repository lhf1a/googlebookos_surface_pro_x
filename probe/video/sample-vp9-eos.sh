#!/bin/busybox sh
# Bounded lab trace of scalar decoder callback fields; no module reloads.
set -eu
mode=${1:-callback}
case "$mode" in callback|packet) ;; *) exit 2;; esac
trace=/proc/1/root/sys/kernel/tracing
group=sq2_vp9_eos
suffix=
if [ "$mode" = packet ]; then group=sq2_vp9_packet; suffix=-packet; fi
instance=$trace/instances/$group
output=/proc/1/root/data/local/tmp/sq2-vp9-eos$suffix-trace.txt
test ! -e "$output"
test "$(cat /sys/module/suspend/parameters/screen_only)" = Y
test ! -e "$trace/events/$group"
mkdir "$instance"
registered=0
cleanup() {
 echo 0 > "$instance/tracing_on"
 if [ "$registered" = 1 ]; then
  echo 0 > "$instance/events/$group/fbd/enable"
  cat "$instance/trace" > "$output"
  echo "-:$group/fbd" >> "$trace/kprobe_events"
 fi
 rmdir "$instance"
 sync
}
trap cleanup EXIT
trap 'exit 130' INT TERM HUP
echo 0 > "$instance/tracing_on"
echo 128 > "$instance/buffer_size_kb"
if [ "$mode" = packet ]; then
 # Fixed hfi_msg_session_fbd_uncompressed_plane0_pkt, all fields u32.
 # hfi_session_hdr_pkt is 12 bytes. Only the resident decoder test runs.
 echo 'p:sq2_vp9_packet/fbd venus_core:hfi_session_ftb_done size=+0(%x2):u32 stream=+12(%x2):u32 error=+20(%x2):x32 flags=+32(%x2):x32 bytes=+52(%x2):u32 offset=+56(%x2):u32' >> "$trace/kprobe_events"
else
 echo 'p:sq2_vp9_eos/fbd venus_dec:vdec_buf_done type=%x1:u32 bytes=%x3:u32 offset=%x4:u32 flags=%x5:x32 hfi_flags=%x6:x32' >> "$trace/kprobe_events"
fi
registered=1
echo 1 > "$instance/events/$group/fbd/enable"
echo 1 > "$instance/tracing_on"
set +e
timeout -k 3 25 /tmp/vp9-decode-test --dmabuf /dev/video0 > /proc/1/root/data/local/tmp/sq2-vp9-eos$suffix-decode.txt 2>&1
result=$?
set -e
echo DECODE_EXIT=$result
