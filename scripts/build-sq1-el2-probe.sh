#!/bin/sh
# Offline only: build upstream's active Secure Launch test, never install/run it.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
src=${1:-$repo/upstream/slbounce}
out=${2:-$repo/out/sq1-el2-preparation}
expected=c090a8cdafa25e4c99df90f8d6f73f3805d9b397
test "$(git -C "$src" rev-parse HEAD)" = "$expected" || { echo 'Unexpected slbounce revision' >&2; exit 1; }
test -z "$(git -C "$src" status --porcelain --untracked-files=no --ignore-submodules=all)" || { echo 'Modified upstream source' >&2; exit 1; }
for item in 'external/arm64-sysreg-lib d421e249a026f6f14653cb6f9c4edd8c5d898595' 'external/gnu-efi 8b018e67212957de176292f95718df48f49a418b'; do
 set -- $item
 test "$(git -C "$src/$1" rev-parse HEAD)" = "$2"
 test -f "$src/$1/.git" || exit 1
done
test ! -e "$out" || { echo 'Use a fresh output directory' >&2; exit 1; }
mkdir -p "$out"
src=$(CDPATH= cd -- "$src" && pwd)
make -C "$src" "$src/out/sltest.efi" >"$out/build.txt" 2>&1
cp "$src/out/sltest.efi" "$out/sltest.efi"
file "$out/sltest.efi" >"$out/file.txt"
grep -q 'PE32+.*EFI application.*Aarch64' "$out/file.txt"
sha256sum "$out/sltest.efi" >"$out/SHA256SUMS"
printf '%s\n' "$expected" >"$out/upstream-revision.txt"
printf '%s\n' 'BUILD ONLY. Active firmware takeover test; not deployed or hardware validated.' >"$out/STATUS.txt"
cat "$out/file.txt" "$out/SHA256SUMS"
