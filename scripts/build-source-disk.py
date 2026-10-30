#!/usr/bin/env python3
"""Build this lab's authorized 128035676160-byte replacement SSD image.

Regular files only. Reuses the proven GPT/ext4 image builder, never a disk.
"""
import importlib.util
from pathlib import Path
import gzip
import shutil
import json
import hashlib

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
    video_spec=importlib.util.spec_from_file_location('video_payload',R/'scripts/sq2-video-boot-payload.py')
    video=importlib.util.module_from_spec(video_spec);video_spec.loader.exec_module(video)
    for n in ('bin/video-runtime-framework-boot','bin/video-codec-manifest-stage',
              'etc/sq2-c2-runtime-boot/runtime.tar','etc/sq2-video-native/SHA256SUMS'):
        if n not in entries:raise ValueError('default hardware video payload missing: '+n)
    if entries['etc/sq2-c2-runtime-boot/media_codecs_c2.xml'][1] != (R/'configs/sq2-media-codecs-c2.xml').read_bytes().replace(b'\r\n',b'\n'):
        raise ValueError('boot codec XML differs from source')
    # Build/deploy consume this generated menu, never a stale untracked menu.
    menu=b.GRUB.read_text()
    generated=b.O/'grub-hardware-video.cfg'
    generated.write_text(video.default_menu(menu))
    b.GRUB=generated
    for n in ('sq2-ssd-layout','sq2-ssd-partitions','sq2-ssd-super-slices'):
        assert entries['etc/'+n][1] == (R/'out/ssd'/n).read_bytes(), n
    d=b.C/'efi-tree/boot/sq2-android1'
    d.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(b.O/'sc8180x-surface-pro-x-rescue.dtb',d/'sc8180x-surface-pro-x-rescue.dtb')
    b.build()
    # Bind deployment policy to the menu actually packed into the ESP.
    manifest_path=b.O/'image-manifest.json'
    manifest=json.loads(manifest_path.read_text())
    manifest['hardware_video']={'enabled':True,'schema':1,
        'menu_sha256':hashlib.sha256(generated.read_bytes()).hexdigest()}
    manifest_path.write_text(json.dumps(manifest,indent=2)+'\n')
