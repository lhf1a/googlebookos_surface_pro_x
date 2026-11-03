#!/usr/bin/env bash
# Isolated SQ1 EL2 PM diagnostic build. Never installs or changes a default.
set -Eeuo pipefail
R=$(cd "$(dirname "$0")/.." && pwd)
W=${SQ1_PM_WORK:-"$R/work/sq1-pm-trial-fresh"}
BASE=${SQ1_PM_BASE_KERNEL:-/root/.cache/sq1-default-el2-20261102/kernel}
CONFIG=${SQ1_PM_CONFIG:-"$R/private/build-inputs/runtime/common/kernel.config"}
O=${SQ1_PM_OUTPUT:-"$R/out/sq1-pm-trial-fresh"}
test ! -e "$W" || { echo 'Use a fresh work directory' >&2; exit 2; }
test ! -e "$O" || { echo 'Use a fresh output directory' >&2; exit 2; }
echo '0ccf48971d8c6b1ab2aa77ba9a4d78ad5909e3cbc4539ea5af4d8124cc412462  '"$BASE/kernel/power/suspend.c" | sha256sum -c -
echo 'b8c8ea7481759a06ff925846c25cf50fb0a6e0075509116b864b4b3e872a3d39  '"$CONFIG" | sha256sum -c -
mkdir -p "$W" "$O"
cp -a "$BASE" "$W/kernel"
git clone --branch v3.2.0-legacy --single-branch https://github.com/KernelSU-Next/KernelSU-Next.git "$W/ksu"
test "$(git -C "$W/ksu" rev-parse HEAD)" = 9b08e88862000d5c50fb2e43a5b75123cf472e54
# Dereference include/uapi: its repository-relative symlink breaks after relocation.
cp -aL "$W/ksu/kernel" "$W/kernel/drivers/kernelsu"
python3 "$R/scripts/patch-sq2-ksunext-63.py" "$W/kernel/drivers/kernelsu"
printf '\nobj-$(CONFIG_KSU) += kernelsu/\n' >> "$W/kernel/drivers/Makefile"
printf '\nsource "drivers/kernelsu/Kconfig"\n' >> "$W/kernel/drivers/Kconfig"
patch --batch --fuzz=0 -d "$W/kernel" -p1 -i "$R/patches/suspend-el2-trial/01-debug-single-owner.patch"
python3 "$R/scripts/check-suspend-trial-policy.py" "$W/kernel/kernel/power/suspend.c"
mkdir "$W/build"
cp "$CONFIG" "$W/build/.config"
export ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
export KBUILD_BUILD_USER=sq2-probe KBUILD_BUILD_HOST=builder
export KBUILD_BUILD_TIMESTAMP='Tue Nov 3 00:00:00 UTC 2026' KBUILD_BUILD_VERSION=1
make -s -C "$W/kernel" O="$W/build" olddefconfig
cmp "$CONFIG" "$W/build/.config"
make -s -C "$W/kernel" O="$W/build" -j12 Image > "$O/build.log" 2>&1
cp "$W/build/arch/arm64/boot/Image" "$O/Image-trial"
cp "$W/build/.config" "$O/kernel.config"
sha256sum "$O/Image-trial" "$O/kernel.config"
echo 'Compile only: QEMU, export audit, isolated boot and physical PM remain separate checks.'
