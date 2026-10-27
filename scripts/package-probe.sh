#!/usr/bin/env bash
# Package existing probe files. Never writes to physical disks.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD=${SQ2_BUILD_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/sq2-probe-build}
COMMIT=b86f786594dc9e53c57fcfa2aa58ada9541506bb
mkdir -p "$BUILD" "$ROOT/out"
for artifact in sq2-probe.img sq2-probe-files.zip; do
    [[ ! -e "$ROOT/out/$artifact" ]] || { echo "Archive existing out/$artifact first."; exit 1; }
done
TREE=$(mktemp -d "$BUILD/efi-tree.XXXXXX")
mkdir -p "$TREE/EFI/BOOT" "$TREE/boot/sq2-probe"
cp "$ROOT/downloads/bootaa64.efi" "$TREE/EFI/BOOT/BOOTAA64.EFI"
cp "$ROOT/configs/grub.cfg" "$TREE/EFI/BOOT/grub.cfg"
cp "$ROOT/out/probe/"{Image,initramfs.cpio.gz,sc8180x-surface-pro-x.dtb} "$TREE/boot/sq2-probe/"
printf 'SQ2 Linux hardware probe. Not a Googlebook installer.\n' > "$TREE/SQ2-PROBE-MEDIA.txt"
IMAGE="$ROOT/out/sq2-probe.img"
# File-only guard. Refuse existing output rather than overwriting any file/device.
[[ ! -e "$IMAGE" ]] || { echo 'out/sq2-probe.img already exists; archive it before rebuilding.'; exit 1; }
FAT=$(mktemp "$BUILD/esp.XXXXXX")
truncate -s 510M "$FAT"
mkfs.vfat -F32 -n SQ2PROBE "$FAT"
mcopy -i "$FAT" -s "$TREE/"* ::/
fsck.fat -n "$FAT"
truncate -s 512M "$IMAGE"
sgdisk --clear --new=1:2048:+510M --typecode=1:ef00 --change-name=1:SQ2PROBE "$IMAGE"
dd if="$FAT" of="$IMAGE" bs=1M seek=1 conv=notrunc status=none
sgdisk --verify "$IMAGE"
python3 - "$TREE" "$ROOT/out/sq2-probe-files.zip" <<'PY'
import pathlib, sys, zipfile
root = pathlib.Path(sys.argv[1])
with zipfile.ZipFile(sys.argv[2], 'x', zipfile.ZIP_DEFLATED) as z:
    for path in sorted(root.rglob('*')):
        if path.is_file(): z.write(path, path.relative_to(root))
PY
(cd "$ROOT/out" && sha256sum sq2-probe.img sq2-probe-files.zip probe/Image probe/initramfs.cpio.gz probe/sc8180x-surface-pro-x.dtb > SHA256SUMS)
printf 'Kernel source commit: %s\nBuild folder: %s\n' "$COMMIT" "$BUILD"
