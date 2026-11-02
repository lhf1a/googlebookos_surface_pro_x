#!/bin/sh
# Build only: does not connect, install, load profiles, activate or reset hardware.
set -eu
[ "$#" = 1 ] || { echo 'usage: build-sq2-pdc-own-profile-lab.sh FRESH_OUTPUT_DIR' >&2; exit 2; }
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out=$1
[ ! -e "$out" ] || { echo 'fresh output directory required' >&2; exit 2; }
mkdir -- "$out"
cc -std=c11 -O2 -Wall -Wextra -Werror "$root/tests/sq2_pdc_session_test.c" -o "$out/session-fixture"
"$out/session-fixture" > "$out/session-fixture.stdout.txt" 2> "$out/session-fixture.stderr.txt"
aarch64-linux-gnu-gcc -std=c11 -O2 -static -Wall -Wextra -Werror \
 "$root/probe/test2/sq2-pdc-own-profile-lab.c" -o "$out/sq2-pdc-own-profile-lab"
qemu-aarch64 "$out/sq2-pdc-own-profile-lab" --profile-self-test > "$out/arm-selftest.stdout.txt" 2> "$out/arm-selftest.stderr.txt"
sha256sum "$root/probe/test2/sq2-pdc-own-profile-lab.c" "$out/sq2-pdc-own-profile-lab" > "$out/SHA256SUMS"
echo 'BUILD_ONLY_PASS: session fixture and ARM64/QEMU self-test; no hardware operations'
