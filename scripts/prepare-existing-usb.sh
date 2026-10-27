#!/usr/bin/env bash
# Work only on image files; preserve every existing file on the USB's FAT volume.
set -Eeuo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD=/root/.cache/sq2-usb-deploy-20261026
[[ ! -e "$BUILD" ]] || { echo 'Preparation directory already exists'; exit 1; }
mkdir -p "$BUILD"/{before,after,payload}
SOURCE="$ROOT/private/sq2-usb-backup-20261026/partition2-original.img"
echo '31da0826cecfd5362c3cd3a6cbcc4fe23120d4f77ceb3cf4f993cdf175f10884  '"$SOURCE" | sha256sum -c -
cp "$SOURCE" "$BUILD/partition2-with-sq2.img"
mcopy -i "$SOURCE" -s '::*' "$BUILD/before/"
python3 - "$ROOT/out/sq2-probe-files.zip" "$BUILD/payload" <<'PY'
import sys,zipfile
with zipfile.ZipFile(sys.argv[1]) as z: z.extractall(sys.argv[2])
PY
# All these paths were absent in the original; never overwrite an original file.
for file in EFI/BOOT/BOOTAA64.EFI EFI/BOOT/grub.cfg boot/sq2-probe/Image boot/sq2-probe/initramfs.cpio.gz boot/sq2-probe/sc8180x-surface-pro-x.dtb SQ2-PROBE-MEDIA.txt; do
    [[ ! -e "$BUILD/before/$file" ]] || { echo "Destination already exists: $file"; exit 1; }
done
mcopy -i "$BUILD/partition2-with-sq2.img" "$BUILD/payload/EFI/BOOT/BOOTAA64.EFI" "$BUILD/payload/EFI/BOOT/grub.cfg" ::/EFI/BOOT/
mcopy -i "$BUILD/partition2-with-sq2.img" -s "$BUILD/payload/boot" ::/
mcopy -i "$BUILD/partition2-with-sq2.img" "$BUILD/payload/SQ2-PROBE-MEDIA.txt" ::/
mcopy -i "$BUILD/partition2-with-sq2.img" -s '::*' "$BUILD/after/"
python3 - "$BUILD" "$ROOT/out/usb-preservation.json" <<'PY'
from pathlib import Path
import hashlib,json,sys
base=Path(sys.argv[1]); records=[]
for p in sorted((base/'before').rglob('*')):
    if not p.is_file(): continue
    relative=p.relative_to(base/'before')
    q=base/'after'/relative
    digest=hashlib.sha256(p.read_bytes()).hexdigest()
    assert q.is_file() and q.stat().st_size == p.stat().st_size
    assert hashlib.sha256(q.read_bytes()).hexdigest() == digest, str(relative)
    records.append({'path':str(relative),'bytes':p.stat().st_size,'sha256':digest})
for p in (base/'payload').rglob('*'):
    if p.is_file():
        assert hashlib.sha256(p.read_bytes()).digest() == hashlib.sha256((base/'after'/p.relative_to(base/'payload')).read_bytes()).digest()
result={'originalFilesPreserved':len(records),'originalFiles':records,
        'candidateSHA256':hashlib.sha256((base/'partition2-with-sq2.img').read_bytes()).hexdigest(),
        'partitionBytes':(base/'partition2-with-sq2.img').stat().st_size}
Path(sys.argv[2]).write_text(json.dumps(result,indent=2)+'\n')
print(f'PASS: all {len(records)} original files preserved; six added files verified.')
print('Candidate SHA256:',result['candidateSHA256'])
PY
cp "$BUILD/partition2-with-sq2.img" "$ROOT/private/partition2-with-sq2.img"
mdir -i "$BUILD/partition2-with-sq2.img" ::/EFI/BOOT/
