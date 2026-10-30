#!/usr/bin/env python3
"""Install to a replacement internal NVMe SSD connected through a USB bridge.

The SSD is installed back into the Surface after writing. Native workstation
NVMe disks are rejected: the installation target must report USB transport.
Input: a prepared image-manifest.json, layout.json and matching disk image.
Destructive fresh installation, NOT a mount repair tool. Boot configuration unchanged.
Run on native Linux (or a Linux VM with exclusive USB passthrough).
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import importlib.util


def run(*cmd):
    return subprocess.check_output(list(map(str, cmd)), text=True).strip()


def inventory():
    return json.loads(run('lsblk', '--json', '--bytes', '--paths', '--output',
                         'NAME,TYPE,TRAN,SIZE,MODEL,SERIAL,MOUNTPOINTS,RO,LOG-SEC'))['blockdevices']


def safe_disk(d):
    def busy(n):
        return any(n.get('mountpoints') or []) or any(busy(c) for c in n.get('children', []))
    return (d['type'] == 'disk' and d.get('tran') == 'usb' and not d['ro']
            and int(d['log-sec']) == 512 and bool((d.get('serial') or '').strip())
            and not busy(d))


def growth(layout, size):
    parts = layout['partitions']
    data = next(p for p in parts if p['name'] == 'userdata')
    if data['offset'] % 512 or data['bytes'] % 4096:
        raise ValueError('Unaligned userdata')
    if any(p['offset'] + p['bytes'] > data['offset'] for p in parts if p is not data):
        raise ValueError('userdata must be physically last (partition index may be 1)')
    if size < layout['image_bytes'] or size % 512:
        raise ValueError('USB smaller than prepared image; build a smaller base image first')
    end = ((size // 512 - 33 - data['offset'] // 512) // 8) * 8
    if end * 512 < data['bytes']:
        raise ValueError('Refusing userdata shrink')
    return data, data['offset'] // 512 + end - 1


def digest(f, offset, count, dest=None):
    f.seek(offset)
    if dest:
        dest.seek(offset)
    h = hashlib.sha256()
    while count:
        b = f.read(min(count, 4 * 1024 * 1024))
        if not b:
            raise ValueError('Short read')
        h.update(b)
        if dest:
            if dest.write(b) != len(b):
                raise OSError('Short write')
        count -= len(b)
    return h.hexdigest()


def confirm_erase(selected, supplied=None):
    # Bind consent to path + serial + capacity, not a generic --yes.
    token = 'ERASE ' + json.dumps(
        [selected['name'], selected['serial'], int(selected['size'])],
        ensure_ascii=True, separators=(',', ':'))
    print('ERASE TARGET:', selected['name'], selected.get('model'),
          selected['serial'], selected['size'], 'bytes')
    print('All existing partitions and data will be replaced. This is NOT secure erasure.')
    print('Required confirmation:', token)
    if supplied is None:
        supplied = input('Type the full confirmation above: ')
    if supplied != token:
        raise ValueError('Erase confirmation mismatch; no disk writes permitted')


def validate_video_menu(menu, policy):
    if policy.get('schema') != 1 or policy.get('enabled') is not True:
        raise ValueError('Unsupported hardware video policy')
    if hashlib.sha256(menu).hexdigest() != policy.get('menu_sha256'):
        raise ValueError('Hardware video menu hash mismatch')
    normal = rescue = 0
    for line in menu.decode('utf-8').splitlines():
        tokens = line.split()
        if not tokens or tokens[0] != 'linux':
            continue
        enabled = {'sq2.video.c2=experimental', 'sq2.video.autostart=1'}
        if 'sq2.rescue=1' in tokens:
            rescue += 1
            if any(t.startswith(('sq2.video.c2=', 'sq2.video.autostart=')) for t in tokens):
                raise ValueError('Rescue enables video')
        elif 'sq2.video=0' not in tokens:
            normal += 1
            if not enabled.issubset(tokens):
                raise ValueError('Normal entry lacks hardware video flags')
    if not normal or not rescue:
        raise ValueError('Hardware video requires enabled normal and safe rescue entries')


def verify_hardware_video(manifest, layout, image, required=False):
    policy = manifest.get('hardware_video')
    if policy is None:
        if required:
            raise ValueError('Image has no hardware video deployment policy')
        return
    esp = next(p for p in layout['partitions'] if p['name'] == 'SQ2PROBE')
    # Read the packed image, not a possibly stale external grub.cfg. No mounts/writes.
    menu = subprocess.check_output(['mtype', '-i', f'{image}@@{esp["offset"]}',
                                    '::/EFI/BOOT/grub.cfg'])
    validate_video_menu(menu, policy)
    print('PASS: packed default hardware video menu and rescue policy')


def verify_display_profiles(manifest, layout, image):
    policy = manifest.get('display_profiles')
    if policy is None:
        return
    spec = importlib.util.spec_from_file_location('display_payload',
        Path(__file__).resolve().with_name('sq2-display-boot-payload.py'))
    display = importlib.util.module_from_spec(spec); spec.loader.exec_module(display)
    if (policy.get('schema') != 1 or policy.get('stock_module_sha256') != display.STOCK_SHA
            or policy.get('edp65_module_sha256') != display.MODULE_SHA
            or policy.get('edp65_edid_sha256') != display.EDID_SHA
            or policy.get('password_addon_last') is not True):
        raise ValueError('Unsupported display policy')
    esp = next(p for p in layout['partitions'] if p['name'] == 'SQ2PROBE')
    def packed(name):
        return subprocess.check_output(['mtype', '-i', f'{image}@@{esp["offset"]}', '::/'+name])
    menu = packed('EFI/BOOT/grub.cfg')
    if display.sha(menu) != policy.get('menu_sha256'):
        raise ValueError('Display menu hash mismatch')
    display.validate_menu(menu, policy.get('default'))
    hashes = policy.get('boot_payload_sha256', {})
    if set(hashes) != {display.BASE, display.ADDON}:
        raise ValueError('Missing display payload hashes')
    for name, expected in hashes.items():
        if (manifest.get('boot_payload_sha256', {}).get(name) != expected
                or display.sha(packed('boot/sq2-android1/'+name)) != expected):
            raise ValueError('Packed display payload hash mismatch: '+name)
    print('PASS: packed display defaults, stock rescue and both payload hashes')


def verify_usb_bluetooth(manifest, layout, image, required=False):
    policy = manifest.get('usb_bluetooth')
    if policy is None:
        if required:
            raise ValueError('Image has no default USB/Bluetooth policy')
        return
    if policy.get('schema') != 1 or policy.get('enabled') is not True:
        raise ValueError('Unsupported USB/Bluetooth policy')
    spec = importlib.util.spec_from_file_location('usb_payload',
        Path(__file__).resolve().with_name('sq2-usb-boot-payload.py'))
    usb = importlib.util.module_from_spec(spec); spec.loader.exec_module(usb)
    esp = next(p for p in layout['partitions'] if p['name'] == 'SQ2PROBE')
    def packed(name):
        return subprocess.check_output(['mtype', '-i', f'{image}@@{esp["offset"]}', '::/'+name])
    menu = packed('EFI/BOOT/grub.cfg')
    if usb.sha(menu) != policy.get('menu_sha256'):
        raise ValueError('USB/Bluetooth menu hash mismatch')
    usb.validate_menu(menu)
    base = packed('boot/sq2-android1/'+usb.BASE)
    if (usb.sha(base) != policy.get('initramfs_sha256')
            or manifest.get('boot_payload_sha256', {}).get(usb.BASE) != usb.sha(base)):
        raise ValueError('Packed USB/Bluetooth initramfs hash mismatch')
    import gzip
    c_spec = importlib.util.spec_from_file_location('cpio',
        Path(__file__).resolve().with_name('pack-sq2-vulkan.py'))
    c = importlib.util.module_from_spec(c_spec); c_spec.loader.exec_module(c)
    if usb.verify(c.entries(gzip.decompress(base)), Path(__file__).resolve().parents[1]) != policy.get('source_sha256'):
        raise ValueError('Packed USB/Bluetooth sources differ from policy')
    print('PASS: packed default USB storage/audio and Bluetooth input payload')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--manifest', required=True, type=Path)
    p.add_argument('--layout', required=True, type=Path)
    p.add_argument('--image', required=True, type=Path, help='Linux path to the prepared image')
    p.add_argument('--device', help='Otherwise select interactively from safe USB disks')
    p.add_argument('--execute', action='store_true', help='Destructive installation; default is preview')
    p.add_argument('--require-hardware-video', action='store_true', help='Reject legacy images without default hardware decoding')
    p.add_argument('--require-usb-bluetooth', action='store_true', help='Reject images without default USB storage/audio and Bluetooth input')
    p.add_argument('--confirm-erase', help='Exact device-bound confirmation printed by preview; otherwise prompt')
    a = p.parse_args()
    m = json.loads(a.manifest.read_text()); layout = json.loads(a.layout.read_text())
    if not a.image.is_file() or a.image.stat().st_size != m['image_bytes'] or m['image_bytes'] != layout['image_bytes']:
        raise ValueError('Image/manifest/layout size mismatch')
    verify_hardware_video(m, layout, a.image, a.require_hardware_video)
    verify_display_profiles(m, layout, a.image)
    verify_usb_bluetooth(m, layout, a.image, a.require_usb_bluetooth)
    disks = [d for d in inventory() if safe_disk(d)]
    for i, d in enumerate(disks):
        print(i, d['name'], d['model'], d['serial'], d['size'])
    if not disks:
        raise ValueError('No unmounted writable USB disk with serial and 512-byte sectors')
    if not a.device:
        a.device = disks[int(input('USB index: '))]['name']
    selected = next(d for d in disks if d['name'] == a.device)
    identity = tuple(selected.get(k) for k in ('name', 'serial', 'model', 'size', 'log-sec'))
    data, end = growth(layout, int(selected['size']))
    print('FRESH INSTALL: all existing data on the selected USB-attached replacement SSD will be lost.')
    print('userdata:', data['offset'], '-> last sector', end, '; shared experimental SQ1/SQ2 configuration unchanged')
    print('Prepared boot payloads:', json.dumps(m.get('boot_payload_sha256', {})))
    if not a.execute:
        print('Confirmation for this target: ERASE ' + json.dumps(
            [selected['name'], selected['serial'], int(selected['size'])], separators=(',', ':')))
        print('PREVIEW ONLY: no writes'); return
    if os.geteuid() != 0:
        raise PermissionError('Execute as root')
    for tool in ('sgdisk', 'partprobe', 'udevadm', 'e2fsck', 'resize2fs'):
        if not shutil.which(tool):
            raise ValueError('Missing tool: ' + tool)
    if not stat.S_ISBLK(Path(a.device).stat().st_mode):
        raise ValueError('Not a block device')
    # Require complete preflight of ALL source regions before changing media.
    records = m['extents']
    if not records:
        raise ValueError('Empty write manifest')
    with a.image.open('rb') as src:
        last = 0
        for e in records:
            off, n = e['offset'], e['bytes']
            if off < last or n <= 0 or off % 512 or n % 512 or off + n > m['image_bytes']:
                raise ValueError('Invalid/overlapping extent')
            last = off + n
            if digest(src, off, n) != e['sha256']:
                raise ValueError('Source hash mismatch')
        confirm_erase(selected, a.confirm_erase)
        current = next(d for d in inventory() if d['name'] == a.device)
        if not safe_disk(current) or tuple(current.get(k) for k in ('name', 'serial', 'model', 'size', 'log-sec')) != identity:
            raise ValueError('USB identity/state changed')
        with open(a.device, 'r+b', buffering=0) as dst:
            for e in records:
                if digest(src, e['offset'], e['bytes'], dst) != e['sha256']:
                    raise ValueError('Source changed during write')
            os.fsync(dst.fileno())
            for e in records:
                if digest(dst, e['offset'], e['bytes']) != e['sha256']:
                    raise ValueError('USB readback mismatch')
        # Keep userdata GUID/type and all filesystem bytes; only move GPT backup
        # and change the end of the physically last userdata partition.
        run('sgdisk', '-e', a.device)
        info = run('sgdisk', '-i', str(data['index']), a.device)
        import re
        guid = re.search(r'Partition unique GUID: (\S+)', info)[1]
        typecode = re.search(r'Partition GUID code: (\S+)', info)[1]
        start = int(re.search(r'First sector: (\d+)', info)[1])
        if start != data['offset'] // 512:
            raise ValueError('Written userdata start mismatch')
        i = data['index']
        run('sgdisk', '-d', i, '-n', f'{i}:{start}:{end}', '-u', f'{i}:{guid}',
            '-t', f'{i}:{typecode}', '-c', f'{i}:userdata', a.device)
        run('partprobe', a.device); run('udevadm', 'settle')
        nodes = inventory()
        disk = next(d for d in nodes if d['name'] == a.device)
        # Resolve by PARTUUID, never guess sda1 versus nvme-style names.
        part = run('blkid', '-t', 'PARTUUID=' + guid.lower(), '-o', 'device').splitlines()
        if len(part) != 1 or part[0] not in [c['name'] for c in disk.get('children', [])]:
            raise ValueError('Ambiguous userdata partition')
        rc = subprocess.call(['e2fsck', '-f', '-p', part[0]])
        if rc not in (0, 1):
            raise ValueError('e2fsck failed: ' + str(rc))
        run('resize2fs', part[0]); run('sgdisk', '-v', a.device)
        os.sync()
        print('USB written/readback verified; userdata resized. Hardware boot NOT tested.')


if __name__ == '__main__':
    main()
