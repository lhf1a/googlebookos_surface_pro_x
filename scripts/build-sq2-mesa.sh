#!/usr/bin/env bash
# Android/Bionic Mesa using the Surface's upstream MSM DRM kernel interface.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${SQ2_GRAPHICS_WORK:-/root/.cache/sq2-graphics}
NDK="$WORK/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64"
SRC="$WORK/src/mesa-26.2.4"
OUT="$ROOT/out/sq2-graphics"
export PATH="$WORK/env/bin:$PATH"
mkdir -p "$WORK/pkgconfig" "$OUT/runtime"
test -x "$NDK/bin/aarch64-linux-android35-clang"
echo 'bce5f7fbebb934373b86c999a064d52fb5065878dc57f287f95346648ec832e9' "$WORK/downloads/mesa-26.2.4.tar.xz" | sha256sum -c -
if [ ! -d "$SRC" ]; then tar -xf "$WORK/downloads/mesa-26.2.4.tar.xz" -C "$WORK/src"; fi
# Keep the reference mapper5 provenance, then extend buffer import for SQ2 video.
python3 - "$ROOT/upstream/gbos-vm/patches/mesa-android-mapper5.patch" "$WORK/mapper5.patch" <<'PY'
from pathlib import Path
import sys
Path(sys.argv[2]).write_text(Path(sys.argv[1]).read_text().rstrip() + '\n')
PY
if [ ! -f "$SRC/src/util/u_gralloc/u_gralloc_vm_mapper5.cpp" ]; then
  patch -p1 -d "$SRC" < "$WORK/mapper5.patch"
fi
python3 "$ROOT/scripts/patch-mesa-sq2-multiplane.py" "$SRC/src/util/u_gralloc/u_gralloc_vm_mapper5.cpp"
cat > "$WORK/android-aarch64.cross" <<EOF
[constants]
ndk = '$NDK'
[binaries]
c = ndk / 'bin/aarch64-linux-android35-clang'
cpp = [ndk / 'bin/aarch64-linux-android35-clang++', '-fno-exceptions', '-fno-unwind-tables', '-fno-asynchronous-unwind-tables', '--start-no-unused-arguments', '-static-libstdc++', '--end-no-unused-arguments']
ar = ndk / 'bin/llvm-ar'
strip = ndk / 'bin/llvm-strip'
c_ld = 'lld'
cpp_ld = 'lld'
pkg-config = '/usr/bin/pkg-config'
[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'armv8'
endian = 'little'
[properties]
needs_exe_wrapper = true
pkg_config_libdir = '$WORK/pkgconfig'
EOF
if [ ! -f "$WORK/build-gles/build.ninja" ]; then
  meson setup "$WORK/build-gles" "$SRC" --cross-file "$WORK/android-aarch64.cross" \
    --buildtype=release -Dforce_fallback_for=libdrm,expat -Dallow-fallback-for=libdrm \
    -Dplatforms=android -Dplatform-sdk-version=35 -Dandroid-stub=true \
    -Dandroid-libbacktrace=disabled -Dllvm=disabled -Dglx=disabled -Dgbm=disabled \
    -Dvideo-codecs= -Dgallium-va=disabled -Dzstd=disabled \
    -Dgallium-drivers=freedreno -Dfreedreno-kmds=msm -Dvulkan-drivers= \
    -Degl=enabled -Dgles1=enabled -Dgles2=enabled -Dopengl=true \
    -Degl-lib-suffix=_freedreno -Dgles-lib-suffix=_freedreno \
    > "$OUT/gles-configure.log" 2>&1 || { tail -70 "$OUT/gles-configure.log"; exit 1; }
fi
ninja -j "${SQ2_BUILD_JOBS:-12}" -C "$WORK/build-gles" > "$OUT/gles-build.log" 2>&1 \
  || { tail -70 "$OUT/gles-build.log"; exit 1; }
for pair in src/egl/libEGL_freedreno.so src/mesa/glapi/es1api/libGLESv1_CM_freedreno.so \
  src/mesa/glapi/es2api/libGLESv2_freedreno.so src/gallium/targets/dri/libgallium_dri.so; do
  "$NDK/bin/llvm-strip" --strip-unneeded -o "$OUT/runtime/$(basename "$pair")" "$WORK/build-gles/$pair"
done
DRM=$(find "$WORK/build-gles/subprojects" -name libdrm.so -type f -print -quit)
test -n "$DRM"
"$NDK/bin/llvm-strip" --strip-unneeded -o "$OUT/runtime/libdrm.so" "$DRM"
"$NDK/bin/aarch64-linux-android35-clang" -O2 -Wall -Wextra \
  "$ROOT/probe/graphics/gles-readback.c" -I"$SRC/include" -L"$OUT/runtime" \
  -Wl,-rpath,'$ORIGIN' -lEGL_freedreno -lGLESv2_freedreno -landroid -o "$OUT/runtime/gles-readback"
# NDK libandroid exports AHB, but directly linking libnativewindow avoids
# unrelated Android connectivity dependencies in this diagnostic executable.
patchelf --replace-needed libandroid.so libnativewindow.so "$OUT/runtime/gles-readback"
"$NDK/bin/aarch64-linux-android35-clang" -O2 -Wall -Wextra -Werror \
  "$ROOT/probe/graphics/yuv-readback.c" -I"$SRC/include" -L"$OUT/runtime" \
  -Wl,-rpath,'$ORIGIN' -lEGL_freedreno -lGLESv2_freedreno -landroid -ldl -o "$OUT/runtime/yuv-readback"
patchelf --replace-needed libandroid.so libnativewindow.so "$OUT/runtime/yuv-readback"
"$NDK/bin/aarch64-linux-android35-clang" -O2 -Wall -Wextra \
  "$ROOT/probe/graphics/ahb-readback.c" -landroid -o "$OUT/ahb-readback"
patchelf --replace-needed libandroid.so libnativewindow.so "$OUT/ahb-readback"
(cd "$OUT/runtime"; sha256sum *.so gles-readback yuv-readback > SHA256SUMS)
echo "SQ2_MESA_GLES_BUILD_OK $OUT/runtime"
