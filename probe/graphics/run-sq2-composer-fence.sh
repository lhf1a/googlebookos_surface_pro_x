#!/system/bin/sh
set -eu
[ "$#" = 1 ] || [ "$#" = 2 ] || { echo "usage: $0 /data/local/tmp/sq2-composer-hal-NAME [--gpu-targets|--vpu-hwc|--fence-errors]"; exit 2; }
mode=${2-}
case "$mode" in ''|--gpu-targets|--vpu-hwc|--fence-errors) ;; *) exit 2;; esac
trial=$1
case "$trial" in /data/local/tmp/sq2-composer-hal-*) ;; *) exit 2;; esac
case "$trial" in *..*|*[!a-zA-Z0-9/_-]*) exit 2;; esac
wake=
logger=
probe=
crc=
touched=0
bound=0
bound_libraries=
service=/vendor/bin/hw/vendor.qti.hardware.display.composer-service
old_user=
trace_mounted=0
debug_mounted=0
trace_instance=
capture_trace() {
 if [ -n "$trace_instance" ]; then
  echo 0 > "$trace_instance/tracing_on"
  cat "$trace_instance/trace" > "$trial/fence-trace.txt"
  for f in "$trace_instance"/per_cpu/cpu*/stats; do echo TRACE_CPU_STATS="$f"; cat "$f"; done > "$trial/trace-stats.txt"
  echo '!stacktrace' > "$trace_instance/events/dma_fence/dma_fence_wait_start/trigger"
  echo 0 > "$trace_instance/events/enable"
  rmdir "$trace_instance"
  trace_instance=
 fi
 if [ "$trace_mounted" = 1 ]; then
  for f in tracing_on current_tracer events/enable trace_clock; do
   echo TRACE_GLOBAL_FIELD="$f"; cat "$trial/tracefs/$f"
  done > "$trial/global-trace-after.txt"
  umount "$trial/tracefs"
  trace_mounted=0
 fi
 [ "$debug_mounted" != 1 ] || umount "$trial/debugfs"
 debug_mounted=0
}
cleanup() {
 set +e
 if [ -n "$probe" ]; then kill "$probe" 2>/dev/null;wait "$probe" 2>/dev/null;fi
 if [ -n "$crc" ]; then kill "$crc" 2>/dev/null;wait "$crc" 2>/dev/null;fi
 capture_trace
 if [ "$touched" = 1 ]; then
  setprop ctl.stop surfaceflinger
  setprop ctl.stop vendor.hwcomposer
  sleep 1
  if [ "$bound" = 1 ]; then umount "$service"; bound=0; fi
  for lib in $bound_libraries; do umount "/vendor/lib64/sq2-qti-display/$lib"; done
  bound_libraries=
  sha256sum "$service" /vendor/lib64/sq2-qti-display/libsdedrm.so /vendor/lib64/sq2-qti-display/libsdmutils.so > "$trial/restored-hashes.txt"
  setprop ctl.start vendor.hwcomposer
  sleep 2
  setprop ctl.start surfaceflinger
  setprop ctl.start zygote
  stable=0
  previous=
  for i in $(seq 1 100); do
   current=$(pidof system_server)
   if [ -n "$current" ] && [ "$current" = "$previous" ] &&
      service check activity | grep -q ': found$' &&
      service check window | grep -q ': found$' &&
      service check input | grep -q ': found$' &&
      service check lock_settings | grep -q ': found$'; then
    stable=$((stable + 1))
    [ "$stable" -lt 10 ] || break
   else
    stable=0
   fi
   previous=$current
   sleep 1
  done
  case "$old_user" in ''|*[!0-9]*) ;; *)
   timeout 30 am start-user "$old_user"
   timeout 30 am switch-user "$old_user"
   sleep 3
   if cmd lock_settings is-user-secure --user "$old_user" | grep -qx false; then
    locksettings set-disabled --user "$old_user" true
    input keyevent KEYCODE_WAKEUP
    wm dismiss-keyguard
   fi
  ;; esac
 fi
 if [ -n "$logger" ]; then kill "$logger"; wait "$logger"; fi
 if [ -n "$wake" ]; then kill "$wake"; wait "$wake"; fi
 cat "$trial/wake.txt"
 for s in vendor.hwcomposer vendor.graphics.allocator surfaceflinger zygote sq2-omx vendor-qti-media-c2-hal-1-0 vendor.power-hal-aidl; do
  echo SERVICE="$s" STATE="$(getprop init.svc."$s")" PID="$(getprop init.svc_debug_pid."$s")"
 done
 dumpsys power | grep -E 'mWakefulness=|Wake Locks:'
 dumpsys window policy | grep -E 'showing=|keyguard'
 cat /sys/module/venus_core/parameters/sq2_pm_status
 cat /proc/sys/kernel/random/boot_id
 am get-current-user
 echo RESTORE_COMPLETE
}
trap cleanup EXIT
trap 'exit 130' INT TERM HUP
(cd "$trial"; sha256sum -c SHA256SUMS)
test "$(getprop init.svc.surfaceflinger)" = running
test "$(getprop init.svc.vendor.hwcomposer)" = running
test "$(getprop init.svc.vendor.graphics.allocator)" = running
old_user=$(am get-current-user)
echo ORIGINAL_USER="$old_user"
cat /proc/sys/kernel/random/boot_id > "$trial/boot.txt"
getprop > "$trial/properties-before.txt"
"$trial/qti-display-wakelock" 300 > "$trial/wake.txt" 2>&1 &
wake=$!
sleep 0.3
grep -q WAKELOCK_READY "$trial/wake.txt"
logcat -v threadtime -T 1 SDM:D SDM_DRM:D Sq2ComposerFence:D '*:E' > "$trial/logcat.txt" 2>&1 &
logger=$!
touched=1
setprop ctl.stop zygote
setprop ctl.stop surfaceflinger
sleep 1
setprop ctl.stop vendor.hwcomposer
sleep 1
for lib in libsdedrm.so libsdmutils.so libsdmcore.so libdrmutils.so libdisplaydebug.so libqservice.so libqdutils.so libgpu_tonemapper.so libhistogram.so; do
 if [ -f "$trial/$lib" ]; then
  mount -o bind "$trial/$lib" "/vendor/lib64/sq2-qti-display/$lib"
  bound_libraries="$lib $bound_libraries"
 fi
done
if [ -f "$trial/candidate-composer" ]; then
 mount -o bind "$trial/candidate-composer" "$service"
 bound=1
fi
setprop ctl.start vendor.hwcomposer
for i in $(seq 1 40); do
 [ "$(getprop init.svc.vendor.hwcomposer)" != running ] || break
 sleep 0.2
done
test "$(getprop init.svc.vendor.hwcomposer)" = running
test "$(getprop init.svc.surfaceflinger)" = stopped
pid=$(getprop init.svc_debug_pid.vendor.hwcomposer)
sha256sum "/proc/$pid/exe" > "$trial/composer-hash.txt"
cat "/proc/$pid/maps" > "$trial/composer-maps.txt"
sha256sum "$service" /vendor/lib64/sq2-qti-display/*.so > "$trial/runtime-hashes.txt"
if [ "$mode" = --gpu-targets ] || [ "$mode" = --vpu-hwc ] || [ "$mode" = --fence-errors ]; then
 mkdir "$trial/tracefs" "$trial/debugfs"
 mount -t tracefs tracefs "$trial/tracefs"
 trace_mounted=1
 mount -t debugfs debugfs "$trial/debugfs"
 debug_mounted=1
 for f in tracing_on current_tracer events/enable trace_clock; do
  echo TRACE_GLOBAL_FIELD="$f"; cat "$trial/tracefs/$f"
 done > "$trial/global-trace-before.txt"
 trace_instance="$trial/tracefs/instances/sq2-composer-gpu-$$"
 mkdir "$trace_instance"
 echo mono > "$trace_instance/trace_clock"
 echo 4096 > "$trace_instance/buffer_size_kb"
 echo stacktrace > "$trace_instance/events/dma_fence/dma_fence_wait_start/trigger"
 echo 1 > "$trace_instance/events/dma_fence/enable"
 echo 1 > "$trace_instance/events/drm/enable"
 export SQ2_COMPOSER_TRACE_MARKER="$trace_instance/trace_marker"
 echo 1 > "$trace_instance/tracing_on"
 if [ "$mode" = --fence-errors ]; then
  if [ ! -d /sys/module/sq2_sw_sync_trial ]; then
   insmod "$trial/sq2_sw_sync_trial.ko"
  fi
  cmp "$trial/sw-sync-build-id.bin" /sys/module/sq2_sw_sync_trial/notes/.note.gnu.build-id
  test -e "$trial/debugfs/sq2-sync-trial/sw_sync"
  # The original built-in code has no module_exit; do not unload a fence provider.
  echo STANDARD_SW_SYNC_RESIDENT_UNTIL_NORMAL_REBOOT=1
  export SQ2_COMPOSER_EVIDENCE="$trial"
  for d in "$trial/debugfs"/dri/[0-9]*; do
   [ -r "$d/state" ] && grep -q '^msm ' "$d/name" || continue
   export SQ2_COMPOSER_DRM_STATE="$d/state"
  done
  test -n "${SQ2_COMPOSER_DRM_STATE-}"
  result=0
  timeout -k 2 80 "$trial/sq2-composer-fence-probe" --fence-errors "$trial/debugfs/sq2-sync-trial/sw_sync" > "$trial/probe.txt" 2>&1 || result=$?
  for d in "$trial/debugfs"/dri/[0-9]*; do
   [ ! -r "$d/state" ] || { echo DRM_DEBUG_PATH="$d";cat "$d/name" "$d/state"; }
  done > "$trial/final-drm.txt"
  cat "$trial/debugfs/sq2-sync-trial/info" > "$trial/sw-sync-final-info.txt"
  capture_trace
 elif [ "$mode" = --vpu-hwc ]; then
  result=0
  for vector in regular drc; do
   mkdir "$trial/$vector-snapshots"
   "$trial/qti-dpu-crc-observer" "$trial/debugfs/dri" 600 15000 > "$trial/$vector-crc.txt" 2>&1 &
   crc=$!
   timeout -k 2 60 env QUEST2_VIDC_BRIDGE=1 QUEST2_VIDC_AUX_CAPS=1 QUEST2_VIDC_NATIVE_LAYOUT=1 \
    LD_PRELOAD="$trial/libquest2-vidc-bridge.so" \
    LD_LIBRARY_PATH="$trial:/system/lib64:/vendor/etc/sq2-omx/lib64:/vendor/lib64" \
    "$trial/quest2-vpu-hwc-decode" --library /vendor/etc/sq2-omx/lib64/libOmxCore.so \
    --component OMX.qcom.video.decoder.avc --native-buffers --width 320 --height 240 \
    --input "$trial/$vector.h264" --aus "$trial/$vector-aus.txt" \
    --output "$trial/$vector-frames" --timeout-ms 5000 > "$trial/$vector.txt" 2>&1 &
   probe=$!
   previous=0
   while kill -0 "$probe" 2>/dev/null; do
    count=$(grep -c '^VPU_PRESENT ' "$trial/$vector.txt" || true)
    if [ "$count" -gt "$previous" ]; then
     for d in "$trial/debugfs"/dri/[0-9]*; do
      [ -r "$d/state" ] || continue
      echo DRM_DEBUG_PATH="$d"; cat "$d/name" "$d/state"
      [ ! -r "$d/framebuffer" ] || cat "$d/framebuffer"
     done > "$trial/$vector-snapshots/$count-drm.txt"
     "$trial/sq2-composer-fence-probe" --inspect > "$trial/$vector-snapshots/$count-hwc.txt"
     previous=$count
    fi
    sleep 0.02
   done
   code=0; wait "$probe" || code=$?
   probe=
   echo "$code" > "$trial/$vector.exit"
   echo VECTOR="$vector" EXIT="$code"
   tail -n 8 "$trial/$vector.txt"
   crccode=0;wait "$crc" || crccode=$?
   crc=
   echo "$crccode" > "$trial/$vector-crc.exit"
   if [ "$code" != 0 ] || [ "$crccode" != 0 ]; then result=1;break;fi
  done
  capture_trace
 else
 timeout -k 2 80 "$trial/sq2-composer-fence-probe" --gpu-targets > "$trial/probe.txt" 2>&1 &
 probe=$!
 for i in $(seq 1 800); do
  if grep -q GPU_TARGET_HOLD_READY "$trial/probe.txt"; then
   for d in "$trial/debugfs"/dri/[0-9]*; do
    [ -r "$d/state" ] || continue
    echo DRM_DEBUG_PATH="$d"; cat "$d/name" "$d/state"
    [ ! -r "$d/framebuffer" ] || cat "$d/framebuffer"
   done > "$trial/gpu-drm.txt"
   "$trial/sq2-composer-fence-probe" --inspect > "$trial/gpu-hwc.txt"
   break
  fi
  kill -0 "$probe" 2>/dev/null || break
  sleep 0.1
 done
 result=0
 wait "$probe" || result=$?
 capture_trace
 fi
else
 result=0
 timeout -k 2 80 "$trial/sq2-composer-fence-probe" > "$trial/probe.txt" 2>&1 || result=$?
fi
echo "$result" > "$trial/probe.exit"
echo PROBE_EXIT="$result"
[ ! -f "$trial/probe.txt" ] || tail -n 12 "$trial/probe.txt"
exit "$result"
