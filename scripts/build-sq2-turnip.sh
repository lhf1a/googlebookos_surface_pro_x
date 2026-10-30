#!/usr/bin/env bash
# Separate Android/MSM Turnip build; does not replace the validated GLES output.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${SQ2_GRAPHICS_WORK:-/root/.cache/sq2-graphics}
NDK="$WORK/android-ndk-r28c/toolchains/llvm/prebuilt/linux-x86_64"
SRC="$WORK/src/mesa-26.2.4"
OUT="$ROOT/out/sq2-turnip"
export PATH="$WORK/env/bin:$PATH"
mkdir -p "$WORK/pkgconfig" "$OUT/runtime" "$SRC/subprojects/packagecache"
test -x "$NDK/bin/aarch64-linux-android35-clang"
echo "bce5f7fbebb934373b86c999a064d52fb5065878dc57f287f95346648ec832e9  $ROOT/downloads/mesa-26.2.4.tar.xz" | sha256sum -c -
echo "fc68f9d0ba2ea63c9432a299e14fea09fad7a8a66e8039fcd7802ca59f77b4f5  $ROOT/downloads/libdrm-2.4.133.tar.xz" | sha256sum -c -
cp "$ROOT/downloads/libdrm-2.4.133.tar.xz" "$SRC/subprojects/packagecache/"
if [ ! -f "$SRC/src/util/u_gralloc/u_gralloc_vm_mapper5.cpp" ]; then
  python3 - "$ROOT/upstream/gbos-vm/patches/mesa-android-mapper5.patch" "$WORK/mapper5.patch" <<'PY'
from pathlib import Path
import sys
Path(sys.argv[2]).write_text(Path(sys.argv[1]).read_text().rstrip()+'\n')
PY
  patch -p1 -d "$SRC" < "$WORK/mapper5.patch"
fi
python3 "$ROOT/scripts/patch-mesa-sq2-multiplane.py" "$SRC/src/util/u_gralloc/u_gralloc_vm_mapper5.cpp"
python3 "$ROOT/scripts/patch-sq2-turnip-gmem.py" "$SRC"
cat > "$WORK/turnip-aarch64.cross" <<EOF
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
if [ ! -f "$WORK/build-turnip/build.ninja" ]; then
  meson setup "$WORK/build-turnip" "$SRC" --cross-file "$WORK/turnip-aarch64.cross" \
    --buildtype=release --wrap-mode=nofallback -Dforce_fallback_for=libdrm -Dallow-fallback-for=libdrm \
    -Dplatforms=android -Dplatform-sdk-version=35 -Dandroid-stub=true \
    -Dandroid-libbacktrace=disabled -Dllvm=disabled -Dglx=disabled -Dgbm=disabled \
    -Dvideo-codecs= -Dgallium-va=disabled -Dzstd=disabled -Dexpat=disabled \
    -Dgallium-drivers= -Dfreedreno-kmds=msm -Dvulkan-drivers=freedreno \
    -Degl=disabled -Dgles1=disabled -Dgles2=disabled -Dopengl=false \
    > "$OUT/configure.log" 2>&1 || { tail -70 "$OUT/configure.log"; exit 1; }
fi
ninja -j "${SQ2_BUILD_JOBS:-8}" -C "$WORK/build-turnip" \
  > "$OUT/build.log" 2>&1 || { tail -70 "$OUT/build.log"; exit 1; }
"$NDK/bin/llvm-strip" --strip-unneeded -o "$OUT/runtime/vulkan.freedreno.so" \
  "$WORK/build-turnip/src/freedreno/vulkan/libvulkan_freedreno.so"
DRM=$(find "$WORK/build-turnip/subprojects" -name libdrm.so -type f -print -quit)
test -n "$DRM"
"$NDK/bin/llvm-strip" --strip-unneeded -o "$OUT/runtime/libdrm.so" "$DRM"
(cd "$OUT/runtime"; sha256sum *.so > SHA256SUMS)
echo "SQ2_TURNIP_BUILD_OK $OUT/runtime"
