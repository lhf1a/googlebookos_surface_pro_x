#!/usr/bin/env python3
"""Build this lab's authorized 128035676160-byte replacement SSD image.

Regular files only. Reuses the proven GPT/ext4 image builder, never a disk.
"""
import importlib.util
from pathlib import Path
import gzip
import shutil

R = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('builder', R/'scripts/build-googlebook-ssd.py')
b = importlib.util.module_from_spec(spec); spec.loader.exec_module(b)
b.O = R/'out/source-boot'
b.BOOT = b.O
b.C = Path('/root/.cache/sq2-probe-build/source-disk-128-20261030')
b.GRUB = b.O/'grub.cfg'
b.IMAGE_NAME = 'sq1-sq2-experimental-no-touch-fix-128.img'

if __name__ == '__main__':
    # Boot-time exact capacity/partition/UUID guards must match the new disk.
    c_spec = importlib.util.spec_from_file_location('cpio', R/'scripts/pack-sq2-vulkan.py')
    c = importlib.util.module_from_spec(c_spec); c_spec.loader.exec_module(c)
    entries = c.entries(gzip.decompress((b.O/'native-combined-initramfs.cpio.gz').read_bytes()))
    for n in ('sq2-ssd-layout','sq2-ssd-partitions','sq2-ssd-super-slices'):
        assert entries['etc/'+n][1] == (R/'out/ssd'/n).read_bytes(), n
    d=b.C/'efi-tree/boot/sq2-android1'
    d.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(b.O/'sc8180x-surface-pro-x-rescue.dtb',d/'sc8180x-surface-pro-x-rescue.dtb')
    b.build()
