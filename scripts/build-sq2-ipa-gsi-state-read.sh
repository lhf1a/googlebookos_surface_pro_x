#!/bin/sh
# Build only. Never install/load or change any runtime/boot payload.
set -eu
[ "$#" -eq 3 ] || { echo 'usage: build-sq2-ipa-gsi-state-read.sh SOURCE BUILD FRESH_OUTPUT' >&2; exit 2; }
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
src=$(realpath "$1"); build=$(realpath "$2"); out=$(realpath -m "$3")
test -f "$build/Module.symvers"
[ ! -e "$out" ] || { echo 'fresh output required' >&2; exit 2; }
mkdir -p "$out"
cp "$root/probe/test2/sq2-ipa-gsi-state-read.c" "$root/probe/test2/sq2-ipa-gsi-read.h" "$root/probe/test2/sq2-ipa-version.h" "$out/"
printf 'obj-m += sq2-ipa-gsi-state-read.o\n' > "$out/Makefile"
cc -Wall -Wextra -Werror -fsanitize=address,undefined "$root/tests/sq2_ipa_gsi_read_test.c" -o "$out/host-test"
"$out/host-test"
make -C "$src" O="$build" ARCH=arm64 CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}" M="$out" modules
sha256sum "$out/sq2-ipa-gsi-state-read.ko"
echo 'BUILT_ONLY: confirmed SQ2/IPA4.1 required; AP-only, no GSI writes or DMA'
