#!/system/bin/sh
# Entry only through quest2-mapper-hal-namespace STAGE THIS_SCRIPT --mapper5 --egl.
# No system service stop, property change, or persistent bind mount.
set -eu
cd "$1"
[ "$(id -u)" = 0 ]
[ "$(readlink /proc/self/ns/mnt)" != "$(readlink /proc/1/ns/mnt)" ]
# The namespace helper already verified binder st_rdev changed before exec.
# /proc/1/root/dev/binder may be an absolute symlink resolved in this namespace.
[ "$(getprop ro.hardware.vulkan)" = freedreno ]
[ -f /odm/lib64/mapper.quest2.so ]
[ -f /odm/lib64/hw/gralloc.kona.so ]
[ ! -e /vendor/lib64/hw/vulkan.freedreno.so ]
cp "$PWD/qgl_config.txt" /odm/lib64/qgl_config.txt
hal_entry=$(cat "$PWD/vulkan-hal-entry.txt")
case "$hal_entry" in vulkan.qti-native.so|vulkan.qti-policy.so) ;; *) exit 1;; esac
cp "$PWD/$hal_entry" /odm/lib64/hw/vulkan.freedreno.so
ln -s /odm/lib64/hw/vulkan.freedreno.so /vendor/lib64/hw/vulkan.freedreno.so
manager_watch=
allocator_watch=
stop_child() {
    role="$1"
    expected="$2"
    if [ -f "native-vulkan-$role.pid" ]; then
        pid=$(cat "native-vulkan-$role.pid")
        case "$pid" in ''|*[!0-9]*) return 1;; esac
        if [ "$(readlink "/proc/$pid/exe" 2>/dev/null || true)" = "$expected" ] &&
           [ "$(readlink "/proc/$pid/ns/mnt" 2>/dev/null || true)" = "$(readlink /proc/self/ns/mnt)" ]; then
            kill "$pid" 2>/dev/null || true
        fi
    fi
}
cleanup() {
    stop_child allocator "$PWD/quest2-allocator5-service" || true
    stop_child manager /system/bin/servicemanager || true
    if [ -n "$allocator_watch" ]; then wait "$allocator_watch" 2>/dev/null || true; fi
    if [ -n "$manager_watch" ]; then wait "$manager_watch" 2>/dev/null || true; fi
}
trap cleanup EXIT
trap 'exit 130' INT TERM HUP
rm -f native-vulkan-manager.pid native-vulkan-allocator.pid
timeout -k 2 360 /system/bin/sh -c \
    'echo $$ > native-vulkan-manager.pid; exec /system/bin/servicemanager' > native-vulkan-manager.txt 2>&1 &
manager_watch=$!
sleep 1
timeout -k 2 350 /system/bin/sh -c \
    'echo $$ > native-vulkan-allocator.pid; exec /system/bin/env LD_LIBRARY_PATH="$PWD:/system/lib64:/vendor/lib64" "$PWD/quest2-allocator5-service" default' > native-vulkan-allocator.txt 2>&1 &
allocator_watch=$!
sleep 1
set +e
timeout -k 2 90 /system/bin/env SQ2_VULKAN_REJECT_TURNIP=1 SQ2_VULKAN_AHB_REEXPORT=1 \
    "$PWD/vulkan-readback" /system/lib64/libvulkan.so --ahb-cycles 3
result=$?
if [ "$result" -eq 0 ] && [ "$hal_entry" = vulkan.qti-policy.so ]; then
    timeout -k 2 60 /system/bin/env SQ2_VULKAN_REJECT_TURNIP=1 SQ2_VULKAN_MEMORY_TRACE=1 \
        SQ2_VULKAN_MEMORY_TYPES=1 SQ2_VULKAN_MEMORY_CYCLES=150 SQ2_VULKAN_MEMORY_BYTES=4096 \
        "$PWD/vulkan-readback" /system/lib64/libvulkan.so
    result=$?
fi
if [ "$result" -eq 0 ] && [ "$hal_entry" = vulkan.qti-policy.so ]; then
    timeout -k 2 90 /system/bin/env SQ2_VULKAN_REJECT_TURNIP=1 SQ2_VULKAN_MEMORY_TRACE=1 \
        SQ2_VULKAN_MEMORY_TYPES=1 SQ2_VULKAN_MEMORY_CYCLES=16 SQ2_VULKAN_MEMORY_BYTES=16777216 \
        "$PWD/vulkan-readback" /system/lib64/libvulkan.so
    result=$?
fi
if [ "$result" -eq 0 ]; then
    timeout -k 2 60 /system/bin/env SQ2_VULKAN_REJECT_TURNIP=1 \
        SQ2_VULKAN_WINDOW_WIDTH=257 SQ2_VULKAN_WINDOW_HEIGHT=129 \
        SQ2_VULKAN_WINDOW_FRAMES=12 SQ2_VULKAN_WINDOW_CYCLES=2 \
        "$PWD/quest2-native-vulkan-window"
    result=$?
fi
set -e
cat native-vulkan-manager.txt native-vulkan-allocator.txt
echo NATIVE_VULKAN_CLIENT_EXIT="$result"
exit "$result"
