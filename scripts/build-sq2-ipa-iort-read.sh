#!/bin/sh
# Build only. No install/load or firmware/boot-image operation.
set -eu
[ "$#" -eq 3 ] || { echo 'usage: build-sq2-ipa-iort-read.sh SOURCE BUILD FRESH_OUTPUT' >&2; exit 2; }
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
src=$(realpath "$1"); build=$(realpath "$2"); out=$(realpath -m "$3")
test -f "$build/Module.symvers"
[ ! -e "$out" ] || { echo 'fresh output required' >&2; exit 2; }
mkdir -p "$out"
cp "$root/probe/test2/sq2-ipa-iort-read.c" "$root/probe/test2/sq2-ipa-acpi.h" "$out/"
printf 'obj-m += sq2-ipa-iort-read.o\n' > "$out/Makefile"
cc -Wall -Wextra -Werror -fsanitize=address,undefined "$root/tests/sq2_ipa_acpi_test.c" -o "$out/host-test"
"$out/host-test"
make -C "$src" O="$build" ARCH=arm64 CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}" M="$out" modules
sha256sum "$out/sq2-ipa-iort-read.ko"
echo 'BUILT_ONLY: explicit SQ2/EL2 allow required; ACPI RAM descriptors only; no MMIO or EFI service'
