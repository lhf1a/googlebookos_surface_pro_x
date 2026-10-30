#!/usr/bin/env python3
"""Build this lab's authorized 128035676160-byte replacement SSD image.

Regular files only. Reuses the proven GPT/ext4 image builder, never a disk.
"""
import importlib.util
from pathlib import Path
import gzip
import json
import hashlib
import argparse

R = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('builder', R/'scripts/build-googlebook-ssd.py')
b = importlib.util.module_from_spec(spec); spec.loader.exec_module(b)
b.O = R/'out/source-boot'
b.BOOT = b.O
b.C = Path('/root/.cache/sq2-display-source-disk-128-20261030')

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--boot-dir',type=Path,default=b.O)
    parser.add_argument('--cache-dir',type=Path,default=b.C)
    parser.add_argument('--display-profile',choices=('5.4-65','2.7-60'),
                        help='Optional consistency check; default comes from build-audit.json')
    args=parser.parse_args()
    b.O=b.BOOT=args.boot_dir.resolve();b.C=args.cache_dir.resolve()
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
    display_spec=importlib.util.spec_from_file_location('display_payload',R/'scripts/sq2-display-boot-payload.py')
    display=importlib.util.module_from_spec(display_spec);display_spec.loader.exec_module(display)
    audit=json.loads((b.O/'build-audit.json').read_text())
    profile=audit['display_profile']
    b.IMAGE_NAME='sq2-display-'+profile+'-experimental-128.img'
    if args.display_profile is not None and args.display_profile != profile:
        raise ValueError('Display profile differs from boot build audit')
    base=(b.O/display.BASE).read_bytes();addon=(b.O/display.ADDON).read_bytes()
    if display.sha(base)!=audit['initramfs_sha256'] or display.sha(addon)!=audit['display_addon_sha256']:
        raise ValueError('Display payload differs from boot build audit')
    # Rebuild expected addon, checking stock/65 hashes, exact delta and final credentials.
    effective=c.entries(gzip.decompress(base+addon))
    if display.build_addon(base,c,effective[display.MODULE][1],effective[display.EDID][1])!=addon:
        raise ValueError('Display addon is not the exact pinned delta')
    menu=(R/'configs/grub-source.cfg').read_text()
    generated=b.O/'grub-hardware-video.cfg'
    generated.write_text(display.default_menu(video.default_menu(menu),profile),encoding='utf-8',newline='\n')
    display_policy=display.policy(generated.read_bytes(),base,addon,profile)
    b.GRUB=generated
    b.BOOT_FILES+=(display.ADDON,'sc8180x-surface-pro-x-rescue.dtb')
    for n in ('sq2-ssd-layout','sq2-ssd-partitions','sq2-ssd-super-slices'):
        assert entries['etc/'+n][1] == (R/'out/ssd'/n).read_bytes(), n
    b.build()
    # Bind deployment policy to the menu actually packed into the ESP.
    manifest_path=b.O/'image-manifest.json'
    manifest=json.loads(manifest_path.read_text())
    manifest['hardware_video']={'enabled':True,'schema':1,
        'menu_sha256':hashlib.sha256(generated.read_bytes()).hexdigest()}
    manifest['display_profiles']=display_policy
    manifest_path.write_text(json.dumps(manifest,indent=2)+'\n')
