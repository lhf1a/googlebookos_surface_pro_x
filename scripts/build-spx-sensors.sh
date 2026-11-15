#!/bin/sh
# Build only, against the target's complete kernel configuration and symvers.
set -eu
[ "$#" -eq 3 ] || { echo "usage: $0 KERNEL_SOURCE MATCHING_BUILD NEW_OUTPUT" >&2; exit 2; }
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
src=$(realpath "$1")
build=$(realpath "$2")
out=$(realpath -m "$3")
test -f "$build/.config"
test -s "$build/Module.symvers"
[ ! -e "$out" ] || { echo "output already exists: $out" >&2; exit 2; }
mkdir -p "$out"
cp "$root/probe/sensors/spx-tcs3430.c" "$out/"
printf 'obj-m += spx-tcs3430.o\n' > "$out/Makefile"
make -C "$src" O="$build" ARCH=arm64 CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}" M="$out" modules
sha256sum "$out/spx-tcs3430.ko"
