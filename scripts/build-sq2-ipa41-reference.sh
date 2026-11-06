#!/bin/sh
# Build only. Never install/load this deliberately fail-closed reference driver.
set -eu
[ "$#" -eq 3 ] || { echo 'usage: build-sq2-ipa41-reference.sh KERNEL_SOURCE KERNEL_BUILD FRESH_OUTPUT' >&2; exit 2; }
src=$(realpath "$1"); build=$(realpath "$2")
case "$3" in /*) out=$3;; *) out=$PWD/$3;; esac
# Single supported kernel baseline; validate before creating output.
policy=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/kernel_baseline.py
python3 "$policy" source "$src"
python3 "$policy" build "$build"
[ ! -e "$out" ] || { echo 'fresh output required' >&2; exit 2; }
here=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test -f "$src/drivers/net/ipa/ipa_main.c"
test -f "$build/include/generated/utsrelease.h"
test -f "$build/Module.symvers"
mkdir -p "$out/source"
cp -a "$src/drivers/net/ipa/." "$out/source/"
# Patch applies only to the fresh subtree, never the source/build or runtime.
(cd "$out/source" && git -c core.autocrlf=false apply --unsafe-paths "$here/patches/sq2-ipa-v4.1-reference.patch")
python3 "$here/tests/test_sq2_ipa41_reference.py" "$out/source/ipa_main.c" qcom,sc8180x-ipa
make -C "$src" O="$build" ARCH=arm64 CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}" \
    M="$out/source" CONFIG_QCOM_IPA=m modules
test -s "$out/source/ipa.ko"
cp "$out/source/ipa.ko" "$out/ipa-reference-NOT-DEPLOYABLE.ko"
sha256sum "$out/ipa-reference-NOT-DEPLOYABLE.ko"
echo 'IPA41_BUILD_PASS: compiled reference subset; hard probe refusal; no install or hardware operation'
