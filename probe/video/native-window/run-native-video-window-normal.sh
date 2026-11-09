#!/system/bin/sh
# Run a finite SurfaceControl diagnostic against the existing normal Android services.
set -eu
test "$#" -eq 4 || test "$#" -eq 5 || test "$#" -eq 6 || { echo 'PROBE_DIR DISPLAY_WIDTH DISPLAY_HEIGHT LAYER_STACK [SCENE]' >&2; exit 2; }
probe=$1
width=$2
height=$3
stack=$4
scene=${5:-baseline}
case "$scene" in baseline|scale-up-left|scale-down-right|occlusion|fullscreen|fullscreen-mask) ;;
    *) echo 'Unknown scene' >&2; exit 2;;
esac
mask_height=${6:-0}
if [ "$scene" = fullscreen-mask ]; then
 case "$mask_height" in ''|*[!0-9]*|0) echo 'Numeric mask height required' >&2; exit 2;; esac
elif [ "$#" -eq 6 ]; then
 echo 'Mask height only for fullscreen-mask' >&2; exit 2
fi
case "$probe" in /*) ;; *) echo 'Absolute probe directory required' >&2; exit 2;; esac
test -f "$probe/sq2-native-video-window.jar"
test -f "$probe/libsq2-native-video-window.so"
zygote=$(pidof zygote64 2>/dev/null || pidof zygote 2>/dev/null)
case "$zygote" in ''|*[!0-9]*) echo 'One running zygote required' >&2; exit 2;; esac
test -r "/proc/$zygote/environ"
# Import only Android runtime paths; never evaluate or print process environment values.
tr '\000' '\n' < "/proc/$zygote/environ" | {
    unset BOOTCLASSPATH DEX2OATBOOTCLASSPATH SYSTEMSERVERCLASSPATH \
        ANDROID_ROOT ANDROID_DATA ANDROID_ART_ROOT ANDROID_I18N_ROOT ANDROID_TZDATA_ROOT
    while IFS= read -r entry; do
        case "$entry" in
            BOOTCLASSPATH=*|DEX2OATBOOTCLASSPATH=*|SYSTEMSERVERCLASSPATH=*|\
            ANDROID_ROOT=*|ANDROID_DATA=*|ANDROID_ART_ROOT=*|ANDROID_I18N_ROOT=*|ANDROID_TZDATA_ROOT=*)
                export "$entry";;
        esac
    done
    test -n "${BOOTCLASSPATH:-}" || { echo 'Zygote BOOTCLASSPATH unavailable' >&2; exit 2; }
    set -- "$probe/libsq2-native-video-window.so" "$width" "$height" "$stack" "$scene"
    if [ "$scene" = fullscreen-mask ]; then set -- "$@" "$mask_height"; fi
    exec env -u LD_PRELOAD -u LD_LIBRARY_PATH "CLASSPATH=$probe/sq2-native-video-window.jar" \
        /system/bin/app_process /system/bin Sq2NativeVideoWindow \
        "$@"
}
