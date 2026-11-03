#!/bin/sh
# Build only. Never connects to a device or sends requests.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out=${1:-"$root/out/sq2-mfs-readonly-lab"}
mkdir -p "$out"
cc=${CROSS_CC:-aarch64-linux-gnu-gcc}
"$cc" -static -O2 -Wall -Wextra -Werror -Wno-misleading-indentation \
 "$root/probe/test2/sq2-mfs-readonly-lab.c" -o "$out/sq2-mfs-readonly-lab"
qemu-aarch64 "$out/sq2-mfs-readonly-lab" --self-test
sha256sum "$root/probe/test2/sq2-mfs-readonly-lab.c" "$out/sq2-mfs-readonly-lab"
