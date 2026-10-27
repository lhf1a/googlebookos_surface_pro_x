#!/usr/bin/env bash
# Diagnostic bridge: rebuild minigbm's C driver layer with DRV_MSM; retain the
# donor's Android C++ HAL ABI. Must pass gralloc allocation/import tests before
# use in the boot overlay. No binary instruction patching is performed.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
W=/root/.cache/sq2-graphics
S="$W/src/minigbm-android"
M="$W/src/mesa-26.2.4"
N="$W/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64/bin"
OUT="$ROOT/out/sq2-graphics/minigbm"
mkdir -p "$OUT" "$W/build-minigbm"
cp "$ROOT/out/cuttlefish-graphics/lib64/libminigbm_gralloc.so" "$OUT/libminigbm_gralloc_donor.so"
patchelf --set-soname libminigbm_gralloc_donor.so "$OUT/libminigbm_gralloc_donor.so"
incs=(-I"$S" -I"$W/android-headers" -I"$M/include/android_stub" -I"$M/subprojects/libdrm-2.4.133" -I"$M/subprojects/libdrm-2.4.133/include/drm")
for name in drv drv_array_helpers drv_helpers dumb_driver backend_mock mediatek msm rockchip virtgpu virtgpu_cross_domain virtgpu_virgl; do
  "$N/aarch64-linux-android35-clang" -O2 -fPIC -D_GNU_SOURCE -DDRV_MSM -DQCOM_DISABLE_COMPRESSED_NV12 \
    "${incs[@]}" -c "$S/$name.c" -o "$W/build-minigbm/$name.o"
done
"$N/aarch64-linux-android35-clang" -shared -Wl,-soname,libminigbm_gralloc.so \
  "$W/build-minigbm/"*.o -L"$OUT" -Wl,--no-as-needed -lminigbm_gralloc_donor \
  -L"$ROOT/out/sq2-graphics/runtime" -ldrm -llog \
  -L/root/.cache/sq2-probe-build/googlebook/vendor/lib64 -lcutils \
  -Wl,-rpath,'$ORIGIN' -o "$OUT/libminigbm_gralloc.so"
"$N/aarch64-linux-android35-clang" -O2 -Wall -Wextra "${incs[@]}" \
  "$ROOT/probe/graphics/gralloc-readback.c" -ldl -o "$OUT/gralloc-readback"
cp "$ROOT/out/cuttlefish-graphics/lib64/hw/gralloc.default.so" "$OUT/"
(cd "$OUT"; sha256sum *.so gralloc-readback > SHA256SUMS)
echo SQ2_MINIGBM_MSM_BRIDGE_BUILD_OK
