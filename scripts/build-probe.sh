#!/usr/bin/env bash
# Run in Ubuntu/WSL. All output is regular files; never opens a physical disk.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD=${SQ2_BUILD_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/sq2-probe-build}
JOBS=${SQ2_JOBS:-16}
COMMIT=b86f786594dc9e53c57fcfa2aa58ada9541506bb
mkdir -p "$BUILD" "$ROOT/out/probe" "$ROOT/work"
BUILD=$(realpath "$BUILD")
for tool in aarch64-linux-gnu-gcc make cpio gzip python3 mformat mcopy mkfs.vfat sgdisk; do command -v "$tool" >/dev/null; done
# Downloaded inputs are pinned and verified before extraction.
(cd "$ROOT" && sha256sum -c configs/probe-inputs.sha256)
if [[ ! -f "$BUILD/kernel/Makefile" ]]; then
    mkdir -p "$BUILD/kernel"
    tar -xf "$ROOT/downloads/surface-kernel-b86f786.tar.gz" --strip-components=1 -C "$BUILD/kernel"
fi
if [[ ! -f "$BUILD/busybox/Makefile" ]]; then
    mkdir -p "$BUILD/busybox"
    tar -xf "$ROOT/downloads/busybox-1.36.1.tar.bz2" --strip-components=1 -C "$BUILD/busybox"
fi
export ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
export KBUILD_BUILD_USER=sq2-probe KBUILD_BUILD_HOST=builder
export KBUILD_BUILD_TIMESTAMP='2026-10-26 00:00:00 UTC'
mkdir -p "$BUILD/kernel-out"
cp "$ROOT/upstream/aarch64-kernel-configs/full/spx-5.19.0-small.config" "$BUILD/kernel-out/.config"
(
    cd "$BUILD/kernel"
    scripts/kconfig/merge_config.sh -m -O "$BUILD/kernel-out" "$BUILD/kernel-out/.config" "$ROOT/configs/diagnostic.config"
    make O="$BUILD/kernel-out" olddefconfig
    make O="$BUILD/kernel-out" -j"$JOBS" Image modules qcom/sc8180x-surface-pro-x.dtb
)
(
    cd "$BUILD/busybox"
    make defconfig >/dev/null
    sed -i 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/; s/CONFIG_TC=y/# CONFIG_TC is not set/' .config
    make -j"$JOBS" CROSS_COMPILE=aarch64-linux-gnu-
)
STAGE=$(mktemp -d "$BUILD/initramfs.XXXXXX")
mkdir -p "$STAGE"/{bin,sbin,proc,sys,dev,run,tmp,logs,media,etc}
cp "$BUILD/busybox/busybox" "$STAGE/bin/busybox"
cp "$ROOT/probe/init" "$STAGE/init"
cp "$ROOT/probe/collect-probe" "$ROOT/probe/save-probe" "$STAGE/bin/"
chmod 755 "$STAGE/init" "$STAGE/bin/"*
make -C "$BUILD/kernel" O="$BUILD/kernel-out" INSTALL_MOD_PATH="$STAGE" INSTALL_MOD_STRIP=1 modules_install
find "$STAGE/lib/modules" -type l -delete
# gen_init_cpio supplies /dev/console without requiring root on the builder.
python3 "$ROOT/scripts/cpio-list.py" "$STAGE" > "$BUILD/initramfs.list"
"$BUILD/kernel-out/usr/gen_init_cpio" -t 1792972800 "$BUILD/initramfs.list" | gzip -n -9 > "$ROOT/out/probe/initramfs.cpio.gz"
cp "$BUILD/kernel-out/arch/arm64/boot/Image" "$ROOT/out/probe/Image"
cp "$BUILD/kernel-out/arch/arm64/boot/dts/qcom/sc8180x-surface-pro-x.dtb" "$ROOT/out/probe/"
cp "$BUILD/kernel-out/.config" "$ROOT/out/probe/kernel.config"
SQ2_BUILD_DIR="$BUILD" bash "$ROOT/scripts/package-probe.sh"
