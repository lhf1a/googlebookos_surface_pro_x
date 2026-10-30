#!/usr/bin/env bash
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
S=${SQ2_MINIGBM_STAGE:-/root/.cache/sq2-video-minigbm-map-cache}
W=/root/.cache/sq2-graphics
N="$W/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin"
D="$W/src/mesa-26.2.4/subprojects/libdrm-2.4.133"
OUT=${SQ2_MINIGBM_OUT:-"$ROOT/out/video-decode/minigbm-map-cache"}
LIB="$ROOT/out/video-decode/map-cache-libs"
mkdir -p "$OUT/obj"
echo '1ab63ce8e7197086f5213a354fa5da34d68e842243c7eb5b8aa911071815d319  libminigbm_gralloc_donor.so' | (cd "$LIB"; sha256sum -c -)
incs=(-I"$S" -I/root/.cache/sq2-c2-core-reference/libcutils/include -I/root/.cache/sq2-c2-logging-reference/liblog/include -I"$W/src/mesa-26.2.4/include/android_stub" -I"$D" -I"$D/include/drm")
for name in drv drv_array_helpers drv_helpers dumb_driver backend_mock mediatek msm rockchip virtgpu virtgpu_cross_domain virtgpu_virgl; do
 "$N/aarch64-linux-android35-clang" -O2 -fPIC -D_GNU_SOURCE -DDRV_MSM -DQCOM_DISABLE_COMPRESSED_NV12 "${incs[@]}" -c "$S/$name.c" -o "$OUT/obj/$name.o"
done
"$N/aarch64-linux-android35-clang" -shared -Wl,-soname,libminigbm_gralloc.so "$OUT"/obj/*.o -L"$LIB" -Wl,--no-as-needed -lminigbm_gralloc_donor -ldrm -lcutils -llog -Wl,-rpath,'$ORIGIN' -o "$OUT/libminigbm_gralloc.so"
sha256sum "$OUT/libminigbm_gralloc.so"
echo 'Private candidate only; no installation performed'
