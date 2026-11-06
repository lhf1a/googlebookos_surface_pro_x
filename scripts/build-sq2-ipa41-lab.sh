#!/bin/sh
# Build only; NEVER installs, loads, reserves RAM, writes firmware or boots.
set -eu
[ "$#" -eq 3 ] || { echo 'usage: build-sq2-ipa41-lab.sh SOURCE BUILD FRESH_OUTPUT' >&2; exit 2; }
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
src=$(realpath "$1"); build=$(realpath "$2"); out=$(realpath -m "$3")
# Single supported kernel baseline; validate before creating output.
policy=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/kernel_baseline.py
python3 "$policy" source "$src"
python3 "$policy" build "$build"
test -f "$build/Module.symvers"
[ ! -e "$out" ] || { echo 'fresh output required' >&2; exit 2; }
mkdir -p "$out/source"
cp -a "$src/drivers/net/ipa/." "$out/source/"
(cd "$out/source" && git -c core.autocrlf=false apply --unsafe-paths "$root/patches/sq2-ipa-v4.1-lab.patch")
python3 "$root/tests/test_sq2_ipa41_lab.py" "$out/source"
make -C "$src" O="$build" ARCH=arm64 CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}" \
 M="$out/source" CONFIG_QCOM_IPA=m modules
test -s "$out/source/ipa_sq2_lab.ko"
cp "$out/source/ipa_sq2_lab.ko" "$out/ipa_sq2_lab.ko"
sha256sum "$out/ipa_sq2_lab.ko"
echo 'BUILT_ONLY: complete4.1 candidate, explicit modes; runtime functionality unproven'
