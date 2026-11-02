#!/bin/sh
# Build only. Does not install/load a module or modify any boot payload.
set -eu
[ "$#" = 3 ] || { echo 'usage: build-sq2-ipa-version-read.sh KERNEL_SOURCE KERNEL_BUILD FRESH_OUTPUT' >&2; exit 2; }
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
kernel=$(realpath "$1")
build=$(realpath "$2")
output=$(realpath -m "$3")
[ -f "$kernel/Makefile" ] && [ -f "$build/Module.symvers" ] || exit 2
[ ! -e "$output" ] || { echo 'Refusing existing output' >&2; exit 2; }
mkdir -p "$output"
cp "$root/probe/test2/sq2-ipa-version-read.c" "$root/probe/test2/sq2-ipa-version.h" "$output/"
printf 'obj-m += sq2-ipa-version-read.o\n' > "$output/Makefile"
"${HOSTCC:-cc}" -Wall -Wextra -Werror "$root/tests/sq2_ipa_version_test.c" -o "$output/host-test"
"$output/host-test"
make -C "$kernel" O="$build" ARCH=arm64 CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}" M="$output" modules
sha256sum "$output/sq2-ipa-version-read.ko"
echo 'BUILT_ONLY: default refuses before clock/MMIO; explicit lab activation required'
