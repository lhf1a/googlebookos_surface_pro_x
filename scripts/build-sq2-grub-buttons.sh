#!/usr/bin/env bash
# Cross-build the pinned linux-surface Fedora39 GRUB, never deploy it here.
# Host prerequisites: cross GCC/binutils, autoconf, automake, autopoint,
# gettext, autogen, bison, flex, make, pkg-config, libfreetype-dev, liblzma-dev.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
STAGE=${SQ2_GRUB_STAGE:-/root/.cache/sq2-grub-buttons-build}
OUT=${1:?Usage: build-sq2-grub-buttons.sh NEW_OUTPUT_DIRECTORY}
GRUB_REV=71402fac6df4af0c06fea69aec6cf4907a1234e0
GNULIB_REV=9f48fb992a3d7e96610c4ce8be969cff2d61a01b
test ! -e "$STAGE"
test ! -e "$OUT"
cd "$ROOT"
printf '%s\n' \
  '632b12856f1b2f5ae0bb57f492713bbd1d8cc0b1b198800491de8a569ccca82f  downloads/grub-surface-71402fac.tar.gz' \
  '00a25a5c3a18d9d7b0deb456344f7ab02f6f9ef8422fbe6174afafc546c8ee36  downloads/gnulib-9f48fb99.tar.gz' | sha256sum -c -
mkdir -p "$STAGE" "$OUT"
OUT=$(realpath "$OUT")
tar -xzf downloads/grub-surface-71402fac.tar.gz -C "$STAGE"
tar -xzf downloads/gnulib-9f48fb99.tar.gz -C "$STAGE"
SOURCE="$STAGE/grub-$GRUB_REV"
python3 scripts/test-sq2-grub-buttons.py "$SOURCE"
python3 scripts/patch-sq2-grub-buttons.py "$SOURCE"
cd "$SOURCE"
export SOURCE_DATE_EPOCH=1684108800
PYTHON=python3 ./bootstrap --no-git --no-bootstrap-sync --skip-po \
  --gnulib-srcdir="$STAGE/gnulib-$GNULIB_REV"
mkdir "$STAGE/build"
cd "$STAGE/build"
HOST_CPPFLAGS="-I$SOURCE" TARGET_CPPFLAGS="-I$SOURCE" \
  "$SOURCE/configure" --with-platform=efi --with-utils=host \
  --program-prefix=aarch64- --target=aarch64-linux-gnu \
  --disable-werror --disable-rpm-sort --disable-device-mapper --disable-libzfs
make -j8
./grub-mkimage -d grub-core -O arm64-efi -o "$OUT/BOOTAA64.EFI" \
  --prefix= $(cat "$ROOT/configs/grub-sq2-modules.txt")
sha256sum "$OUT/BOOTAA64.EFI" "$SOURCE/grub-core/term/efi/console.c" > "$OUT/SHA256SUMS"
./grub-mkimage --version > "$OUT/version.txt"
cp config.log "$OUT/config.log"
printf 'Built candidate only; real button input and device boot still need validation.\n'
