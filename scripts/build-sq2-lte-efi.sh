#!/usr/bin/env bash
# Build only; no installation, reboot or firmware access.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
G=${GNU_EFI_DIR:-/root/.cache/sq2-efi-repro-venus-v1/external/gnu-efi}
O="$ROOT/out/lte-efi"
mkdir -p "$O"
aarch64-linux-gnu-gcc -Wall -Wextra -Werror -O2 -fpic -fshort-wchar \
    -fno-stack-protector -ffreestanding -mstrict-align \
    -DCONFIG_aarch64 -D__MAKEWITH_GNUEFI -DGNU_EFI_USE_MS_ABI \
    -I"$G/inc" -I"$G/inc/aarch64" -I"$G/inc/protocol" \
    -c "$ROOT/probe/virtualization/sq2-lte-efi-snapshot.c" -o "$O/snapshot.o"
aarch64-linux-gnu-gcc -nostdlib -shared -Wl,-Bsymbolic \
    -Wl,--no-wchar-size-warning -Wl,--defsym=EFI_SUBSYSTEM=10 \
    -T "$G/gnuefi/elf_aarch64_efi.lds" \
    "$G/aarch64/gnuefi/crt0-efi-aarch64.o" "$O/snapshot.o" \
    -L"$G/aarch64/lib" -L"$G/aarch64/gnuefi" -lefi -lgnuefi -o "$O/snapshot.so"
aarch64-linux-gnu-objcopy -j .text -j .sdata -j .data -j .dynamic \
    -j .dynsym -j .rel -j .rela -j .reloc --target=efi-app-aarch64 \
    "$O/snapshot.so" "$O/lte-snapshot.efi"
sha256sum "$O/lte-snapshot.efi"
