#!/system/bin/sh
# Invoke only inside the prepared private SF/mapper/OMX mount namespace.
# The runner captures the existing zygote Android environment before stopping it.
set -eu
test "$#" -eq 4 || { echo 'PROBE_DIR DISPLAY_WIDTH DISPLAY_HEIGHT LAYER_STACK' >&2; exit 2; }
probe=$1
shift
case "$probe" in /*) ;; *) echo 'Absolute probe directory required' >&2; exit 2;; esac
test -n "${BOOTCLASSPATH:-}"
test -f "$probe/sq2-native-video-window.jar"
test -f "$probe/libsq2-native-video-window.so"
exec env -u LD_PRELOAD -u LD_LIBRARY_PATH "CLASSPATH=$probe/sq2-native-video-window.jar"     /system/bin/app_process /system/bin Sq2NativeVideoWindow     "$probe/libsq2-native-video-window.so" "$@"
