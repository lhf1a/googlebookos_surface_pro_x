#!/system/bin/sh
# Run inside Android root. Temporary RAM-vendor declaration; no service restart.
set -eu
target=/vendor/etc/vintf/manifest/sq2-c2-experimental.xml
test "$(cat /sys/module/suspend/parameters/screen_only)" = Y
test ! -e "$target"
test -x /data/local/tmp/sq2-c2-discovery-probe
test -x /data/local/tmp/sq2-c2-aidl-v2
grep ' /vendor .* - overlay .*upperdir=/newroot/debug_ramdisk/vendor-upper' /proc/self/mountinfo >/dev/null
cleanup() {
 rm -f "$target"
 echo AFTER_ROLLBACK
 /data/local/tmp/sq2-c2-discovery-probe
}
trap cleanup EXIT
trap 'exit 130' INT TERM HUP
echo BEFORE
/data/local/tmp/sq2-c2-discovery-probe
cat > "$target" <<'EOF'
<manifest version="2.0" type="device">
 <hal format="aidl">
  <name>android.hardware.media.c2</name>
  <version>2</version>
  <fqname>IComponentStore/default1</fqname>
 </hal>
</manifest>
EOF
chmod 644 "$target"
restorecon "$target"
echo WITH_TEMPORARY_DECLARATION
/data/local/tmp/sq2-c2-discovery-probe
export LD_LIBRARY_PATH=/system/lib64:/apex/com.android.media.swcodec/lib64:/vendor/lib64
set +e
timeout 8 /data/local/tmp/sq2-c2-aidl-v2 --serve &
service_pid=$!
sleep 2
echo DURING_NORMAL_REGISTRATION
/data/local/tmp/sq2-c2-discovery-probe
/data/local/tmp/sq2-c2-aidl-client
echo CLIENT_EXIT=$?
wait "$service_pid"
rc=$?
set -e
echo NORMAL_REGISTRATION_EXIT=$rc
# timeout exits only after the experimental service has been terminated.
test "$rc" = 5 || test "$rc" = 124
