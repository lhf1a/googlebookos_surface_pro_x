#!/usr/bin/env bash
# Build only. No SSH, installation, ARM marker, reboot, or device access.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
G=${GNU_EFI_DIR:-$ROOT/upstream/gnu-efi-lte-8b018e6}
O=${1:-$ROOT/out/lte-efi-v2}
test "$(git -C "$G" rev-parse HEAD)" = 8b018e67212957de176292f95718df48f49a418b
test -z "$(git -C "$G" status --porcelain --untracked-files=no)"
test ! -e "$O" || { echo 'Use a fresh output directory' >&2; exit 1; }
mkdir -p "$O"
aarch64-linux-gnu-gcc -Wall -Wextra -Werror -O2 -fpic -fshort-wchar \
    -fno-stack-protector -ffreestanding -mstrict-align \
    -DCONFIG_aarch64 -D__MAKEWITH_GNUEFI -DGNU_EFI_USE_MS_ABI \
    -I"$G/inc" -I"$G/inc/aarch64" -I"$G/inc/protocol" \
    -c "$ROOT/probe/virtualization/sq2-lte-efi-snapshot-v2.c" -o "$O/snapshot.o"
aarch64-linux-gnu-gcc -nostdlib -shared -Wl,-Bsymbolic \
    -Wl,--no-wchar-size-warning -Wl,--defsym=EFI_SUBSYSTEM=10 \
    -T "$G/gnuefi/elf_aarch64_efi.lds" \
    "$G/aarch64/gnuefi/crt0-efi-aarch64.o" "$O/snapshot.o" \
    -L"$G/aarch64/lib" -L"$G/aarch64/gnuefi" -lefi -lgnuefi -o "$O/snapshot.so"
aarch64-linux-gnu-objcopy -j .text -j .sdata -j .data -j .dynamic \
    -j .dynsym -j .rel -j .rela -j '.rel.*' -j '.rela.*' -j .reloc -O binary \
    "$O/snapshot.so" "$O/lte-snapshot.efi"
file "$O/lte-snapshot.efi" | tee "$O/file.txt"
grep -q 'PE32+.*EFI application.*Aarch64' "$O/file.txt"
sha256sum "$ROOT/probe/virtualization/sq2-lte-efi-snapshot-v2.c" \
    "$ROOT/probe/virtualization/sq2-lte-gpt.h" "$O/lte-snapshot.efi" > "$O/SHA256SUMS"
printf '%s\n' 'BUILD ONLY. Explicit lab candidate, not installed or enabled.' > "$O/STATUS.txt"
cat "$O/SHA256SUMS" "$O/STATUS.txt"
