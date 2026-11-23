#!/bin/sh
# External CAMSS candidate for the matching kernel; does not deploy or stream.
set -eu
[ "$#" -eq 3 ] || { echo "usage: $0 KERNEL_SOURCE MATCHING_BUILD NEW_OUTPUT" >&2; exit 2; }
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
src=$(realpath "$1")
build=$(realpath "$2")
out=$(realpath -m "$3")
test -f "$build/.config"
test -f "$build/Module.symvers"
[ ! -e "$out" ] || { echo "output already exists: $out" >&2; exit 2; }
mkdir -p "$out"
cp "$src"/drivers/media/platform/qcom/camss/*.[ch] "$out/"
cp "$src/drivers/media/common/videobuf2/videobuf2-dma-sg.c" "$out/"
sed 's/obj-$(CONFIG_VIDEO_QCOM_CAMSS)/obj-m/' "$src/drivers/media/platform/qcom/camss/Makefile" > "$out/Makefile"
printf '\nobj-m += videobuf2-dma-sg.o\n' >> "$out/Makefile"
patch --batch --forward -d "$out" -p1 < "$root/patches/camera/0003-camss-sc8180x-initial.patch"
patch --batch --forward -d "$out" -p1 < "$root/patches/camera/0007-camss-balanced-s-stream-unwind.patch"
patch --batch --forward -d "$out" -p1 < "$root/patches/camera/0009-camss-privacy-leds-vfe-lite-fifo.patch"
DEFAULT_JOBS=$(( $(nproc 2>/dev/null || echo 2) - 1 ))
[ "$DEFAULT_JOBS" -lt 1 ] && DEFAULT_JOBS=1
make -C "$src" O="$build" ARCH=arm64 CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}" M="$out" -j"${JOBS:-$DEFAULT_JOBS}" modules
