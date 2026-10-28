#!/bin/sh
# Temporary mount namespace only. Android EGL/GLES dispatches the private donor;
# PID 1, SurfaceFlinger, on-disk vendor and the boot image retain Mesa.
set -eu
base=/data/local/tmp/sq2-closed/gles-linked
if [ -n "${SQ2_GLES_CPU_LAYOUT:-}" ];then
    test "${SQ2_GLES_READBACK_POLICY:-}" != 1
    case "$SQ2_GLES_CPU_LAYOUT" in
        4) base=/data/local/tmp/sq2-closed/gles-linked-cpu-layout ;;
        4-bank15) base=/data/local/tmp/sq2-closed/gles-linked-cpu-layout-bank15 ;;
        4-bank15-pbo) base=/data/local/tmp/sq2-closed/gles-linked-cpu-layout-pbo ;;
        4-bank15-zero-area) base=/data/local/tmp/sq2-closed/gles-linked-zero-area ;;
        4-bank15-fence-prune) base=/data/local/tmp/sq2-closed/gles-linked-fence-prune ;;
        *) echo 'Unknown private CPU layout candidate' >&2; exit 2 ;;
    esac
fi
if [ "${SQ2_GLES_READBACK_POLICY:-}" = 1 ];then
    base=/data/local/tmp/sq2-closed/gles-linked-readback-policy
fi
system_probe=/data/local/tmp/sq2-closed/gles-v77
visible=/data/local/tmp/sq2-closed/visible
readpixels=/data/local/tmp/sq2-closed/readpixels
composition=/data/local/tmp/sq2-closed/composition
vulkan_window=/data/local/tmp/sq2-closed/vulkan-window
vulkan=/data/local/tmp/sq2-closed/v77-vulkan-system
if [ "${SQ2_V77_AHB_VIEW:-}" = 1 ];then
    vulkan=/data/local/tmp/sq2-closed/v77-vulkan-ahb
    ahb_revision=${SQ2_V77_AHB_REVISION:-3}
    case "$ahb_revision" in
        1) ;;
        [2-9]) vulkan="$vulkan-v$ahb_revision" ;;
        *) echo 'Unknown private v77 AHB revision' >&2; exit 2 ;;
    esac
fi
vulkan_probe=/data/local/tmp/sq2-closed/v77-vulkan-probe
combined_probe=/data/local/tmp/sq2-closed/combined-probe
fp16_probe=/data/local/tmp/sq2-closed/fp16-probe
if [ "${SQ2_COMBINED:-}" = 1 ];then
    test -z "${SQ2_GLES_CPU_LAYOUT:-}${SQ2_GLES_DIRECT:-}${SQ2_GLES_GPU_READBACK:-}${SQ2_GLES_READBACK_POLICY:-}"
    base=/data/local/tmp/sq2-closed/combined-v1
    case "${SQ2_COMBINED_REVISION:-1}" in 1) ;; 3) base=/data/local/tmp/sq2-closed/combined-v3;; *) exit 2;; esac
    vulkan=$base
    export SQ2_V77_AHB_VIEW=1
fi
if [ "${1:-}" = --inside ];then
    shift
    test "$(readlink /proc/self/ns/mnt)" != "$(readlink /proc/1/ns/mnt)"
    # The checked launcher made / recursively private, then mounted this tmpfs.
    test "$(stat -f -c %T /vendor/lib64/egl)" = tmpfs
    if [ "${SQ2_COMBINED:-}" = 1 ];then
        cp "$base/"*.so /vendor/lib64/egl/
        ln -s libquest2-egl-wrapper.so /vendor/lib64/egl/libEGL_freedreno.so
        ln -s libGLESv1_CM_adreno.so /vendor/lib64/egl/libGLESv1_CM_freedreno.so
        ln -s libGLESv2_adreno.so /vendor/lib64/egl/libGLESv2_freedreno.so
        mount --bind /vendor/lib64/egl/libquest2-vulkan-wrapper.so /vendor/lib64/hw/vulkan.freedreno.so
        echo "PRIVATE_COMBINED_NAMESPACE=$(readlink /proc/self/ns/mnt)"
        if [ "${1:-}" = --fp16 ];then
            exec env -u LD_PRELOAD -u LD_LIBRARY_PATH "$fp16_probe/quest2-fp16"
        fi
        if [ "${1:-}" = --combined ];then
            exec env -u LD_PRELOAD -u LD_LIBRARY_PATH SQ2_VULKAN_REJECT_TURNIP=1 "$combined_probe/quest2-combined"
        fi
    fi
    case "${1:-}" in --vulkan|--vulkan-offscreen|--vulkan-properties|--vulkan-import|--vulkan-window|--vulkan-window-create)
        cp "$vulkan/"*.so /vendor/lib64/egl/
        vk_entry=vulkan-v77-bridge.so
        if [ "${SQ2_V77_AHB_VIEW:-}" = 1 ];then vk_entry=libquest2-vulkan-wrapper.so;fi
        mount --bind "/vendor/lib64/egl/$vk_entry" /vendor/lib64/hw/vulkan.freedreno.so
        echo "PRIVATE_VULKAN_NAMESPACE=$(readlink /proc/self/ns/mnt)"
        sha256sum /vendor/lib64/hw/vulkan.freedreno.so
        if [ "$1" = --vulkan-window-create ];then
            exec env -u LD_PRELOAD -u LD_LIBRARY_PATH SQ2_VULKAN_REJECT_TURNIP=1 \
                "$vulkan_window/quest2-vulkan-window" --create-only
        fi
        if [ "$1" = --vulkan-window ];then
            exec env -u LD_PRELOAD -u LD_LIBRARY_PATH SQ2_VULKAN_REJECT_TURNIP=1 \
                "$vulkan_window/quest2-vulkan-window"
        fi
        if [ "$1" = --vulkan-offscreen ];then
            exec env -u LD_PRELOAD -u LD_LIBRARY_PATH SQ2_VULKAN_REJECT_TURNIP=1 \
                "$vulkan_probe/vulkan-readback" /system/lib64/libvulkan.so --offscreen-only
        fi
        if [ "$1" = --vulkan-properties ] || [ "$1" = --vulkan-import ];then
            stage=--ahb-properties-only
            if [ "$1" = --vulkan-import ];then stage=--ahb-import-only;fi
            exec env -u LD_PRELOAD -u LD_LIBRARY_PATH SQ2_VULKAN_REJECT_TURNIP=1 \
                "$vulkan_probe/vulkan-readback" /system/lib64/libvulkan.so \
                "$stage" --ahb-cycles "${SQ2_VULKAN_AHB_CYCLES:-1}"
        fi
        exec env -u LD_PRELOAD -u LD_LIBRARY_PATH SQ2_VULKAN_REJECT_TURNIP=1 \
            "$vulkan_probe/vulkan-readback" /system/lib64/libvulkan.so \
            --ahb-cycles "${SQ2_VULKAN_AHB_CYCLES:-1}"
    ;; esac
    cp "$base/"*.so /vendor/lib64/egl/
    if [ "${SQ2_COMBINED:-}" != 1 ];then
        ln -s libquest2-egl-wrapper.so /vendor/lib64/egl/libEGL_freedreno.so
        ln -s libGLESv1_CM_adreno.so /vendor/lib64/egl/libGLESv1_CM_freedreno.so
        ln -s libGLESv2_adreno.so /vendor/lib64/egl/libGLESv2_freedreno.so
    fi
    echo "PRIVATE_EGL_NAMESPACE=$(readlink /proc/self/ns/mnt)"
    if [ "${1:-}" = --composition ];then
        exec env -u LD_PRELOAD -u LD_LIBRARY_PATH "$composition/quest2-composition"
    fi
    if [ "${SQ2_GLES_DIRECT:-}" = 1 ] || [ "${SQ2_GLES_GPU_READBACK:-}" = 1 ];then
        test "$(stat -f -c %T /data/vendor)" = tmpfs
        mkdir /data/vendor/gpu
        printf '0x0=0x08675309\n' > /data/vendor/gpu/esx_config.txt
    fi
    if [ "${SQ2_GLES_DIRECT:-}" = 1 ];then
        # v77 ESX parser: magic header; setting 0x375d04e5 uses the enum table
        # at VA 0x3fabe8 (Direct=0), stored at settings+0x1ee0.
        printf '0x375d04e5=Direct\n' >> /data/vendor/gpu/esx_config.txt
        echo PRIVATE_ESX_DIRECT_CONFIG
    fi
    if [ "${SQ2_GLES_GPU_READBACK:-}" = 1 ];then
        # v77 numeric readback thresholds settings+9596/+9600. Later device
        # policy can override these; this is an experiment, not a known fix.
        printf '0x0388d1af=0\n0x3497955d=0\n' >> /data/vendor/gpu/esx_config.txt
        echo PRIVATE_ESX_GPU_READBACK_CONFIG
    fi
    if [ "${1:-}" = --window ];then
        exec env -u LD_PRELOAD -u LD_LIBRARY_PATH "$system_probe/gles-window"
    fi
    if [ "${1:-}" = --yuv ];then
        exec env -u LD_PRELOAD -u LD_LIBRARY_PATH "$system_probe/yuv-readback"
    fi
    if [ "${1:-}" = --readpixels ];then
        if [ "${SQ2_READPIXELS_CAPTURE:-}" = 1 ];then
            exec env -u LD_PRELOAD -u LD_LIBRARY_PATH SQ2_READPIXELS_TRACE_COPY=1 \
                SQ2_READBACK_SAVE_SOURCE=1 "$readpixels/quest2-readpixels" \
                --size "${SQ2_READPIXELS_CAPTURE_WIDTH:-640}" \
                "${SQ2_READPIXELS_CAPTURE_HEIGHT:-360}" --path client --no-errors
        fi
        if [ "${SQ2_READPIXELS_RELAY:-}" = 1 ];then
            exec env -u LD_PRELOAD -u LD_LIBRARY_PATH "$readpixels/quest2-readpixels" --relay-to-ahb
        fi
        if [ "${SQ2_READPIXELS_UPLOAD:-}" = 1 ];then
            exec env -u LD_PRELOAD -u LD_LIBRARY_PATH "$readpixels/quest2-readpixels" --upload-relay
        fi
        if [ -n "${SQ2_READPIXELS_WIDTH:-}" ];then
            test -n "${SQ2_READPIXELS_HEIGHT:-}"
            exec env -u LD_PRELOAD -u LD_LIBRARY_PATH "$readpixels/quest2-readpixels" \
                --size "$SQ2_READPIXELS_WIDTH" "$SQ2_READPIXELS_HEIGHT"
        fi
        exec env -u LD_PRELOAD -u LD_LIBRARY_PATH "$readpixels/quest2-readpixels"
    fi
    if [ "${1:-}" = --visible ];then
        # Already inside the private namespace: never re-enter PID 1 mounts here.
        exec env -u LD_PRELOAD -u LD_LIBRARY_PATH "CLASSPATH=$visible/quest2-visible.jar" \
            /system/bin/app_process /system/bin Sq2Quest2Visible
    fi
    exec env -u LD_PRELOAD -u LD_LIBRARY_PATH "$system_probe/gles-readback-system" "$@"
fi
case "${1:-}" in ''|--ahb|--window|--yuv|--visible|--readpixels|--composition|--combined|--fp16|--vulkan|--vulkan-offscreen|--vulkan-properties|--vulkan-import|--vulkan-window|--vulkan-window-create) ;; *) echo 'Unsupported system graphics probe mode' >&2;exit 2;; esac
test "$#" -le 1
test "$(cat /sys/module/suspend/parameters/screen_only)" = Y
root=/proc/1/root
expected=f16302e33e4f7c8bbaccbe5ab5fd5d19f3279222ee430ace236c4bad237a5434
expected_vk=0b36eeba9d77ed15e7465396314df80a9479f0b5ccd858d362b40af39fb1759c
check_mesa(){
    test "$(sha256sum "$root/vendor/lib64/egl/libEGL_freedreno.so" | cut -d ' ' -f1)" = "$expected"
    test "$(sha256sum "$root/vendor/lib64/hw/vulkan.freedreno.so" | cut -d ' ' -f1)" = "$expected_vk"
}
check_mesa
if [ "${1:-}" = --fp16 ];then
    test "${SQ2_COMBINED:-}" = 1
    (cd "$root$fp16_probe";sha256sum -c FP16_PROBE_SHA256SUMS)
fi
test "$(nsenter -t 1 -m -r -- /system/bin/getprop ro.hardware.egl)" = freedreno
(cd "$root$base";sha256sum -c SHA256SUMS; if [ "${SQ2_COMBINED:-}" != 1 ];then sha256sum -c PROBE_SHA256SUMS;fi)
if [ "${1:-}" = --combined ];then
    test "${SQ2_COMBINED:-}" = 1
    test "$(nsenter -t 1 -m -r -- /system/bin/getprop ro.hardware.vulkan)" = freedreno
    (cd "$root$combined_probe";sha256sum -c COMBINED_PROBE_SHA256SUMS)
fi
(cd "$root$system_probe";sha256sum -c SYSTEM_PROBE_SHA256SUMS)
case "${1:-}" in --vulkan|--vulkan-offscreen|--vulkan-properties|--vulkan-import|--vulkan-window|--vulkan-window-create)
    test "$(nsenter -t 1 -m -r -- /system/bin/getprop ro.hardware.vulkan)" = freedreno
    (cd "$root$vulkan";sha256sum -c SHA256SUMS)
    (cd "$root$vulkan_probe";sha256sum -c VULKAN_PROBE_SHA256SUMS)
;; --readpixels)
    (cd "$root$readpixels";sha256sum -c READPIXELS_SHA256SUMS)
;; --composition)
    (cd "$root$composition";sha256sum -c COMPOSITION_SHA256SUMS)
;; esac
case "${1:-}" in --vulkan-window|--vulkan-window-create)
    test "${SQ2_V77_AHB_VIEW:-}" = 1
    (cd "$root$vulkan_window";sha256sum -c VULKAN_WINDOW_SHA256SUMS)
;; esac
if [ "${1:-}" = --visible ];then
    (cd "$root$visible";sha256sum -c VISIBLE_SHA256SUMS)
    pid=$(pidof zygote64 | awk '{print $1}')
    test -n "$pid"
    # Capture classpaths before entering Android/private mounts. Export only
    # the existing Android runtime variables, not arbitrary zygote environment.
    envfile=$(mktemp /tmp/sq2-visible-env.XXXXXX)
    tr '\000' '\n' < "/proc/$pid/environ" |
        grep -E '^(ANDROID_ROOT|ANDROID_ASSETS|ANDROID_DATA|ANDROID_STORAGE|ANDROID_ART_ROOT|ANDROID_I18N_ROOT|ANDROID_TZDATA_ROOT|BOOTCLASSPATH|DEX2OATBOOTCLASSPATH)=' > "$envfile"
    while IFS= read -r entry; do export "$entry"; done < "$envfile"
    rm -f "$envfile"
    test -n "${BOOTCLASSPATH:-}"
fi
mkdir /tmp/sq2-quest2-gles.lock
trap 'rmdir /tmp/sq2-quest2-gles.lock' EXIT
boot=$(cat /proc/sys/kernel/random/boot_id)
echo "BOOT_BEFORE=$boot"
pids=$(nsenter -t 1 -m -r -- /system/bin/pidof system_server surfaceflinger)
echo "$pids"
result=0
timeout -k 3 25 nsenter -t 1 -m -r -- "$system_probe/egl-private-namespace" "$@" || result=$?
echo "SYSTEM_PROBE_EXIT=$result"
test "$(cat /proc/sys/kernel/random/boot_id)" = "$boot"
check_mesa
echo DEFAULT_MESA_UNCHANGED
after=$(nsenter -t 1 -m -r -- /system/bin/pidof system_server surfaceflinger)
echo "$after"
test "$after" = "$pids"
exit "$result"
