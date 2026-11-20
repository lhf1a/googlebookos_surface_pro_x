"""Assemble a private Linux 6.18 single-user GPT disk image from explicit inputs.

Only fresh regular files are written. No devices, loop mounts, deployment,
formatting of existing data or reboot. Content hash gates are disabled in this
universal build path; format, ABI and target-path constraints remain enforced.
"""
import copy
import errno
import gzip
import hashlib
import importlib.util
import json
import os
import re
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import uuid
import zipfile
import zlib

from kernel_baseline import ROOT, policy
from single_user_payload import cpio_module, HOOK, ANCHOR

M = 1048576
G = 1024 ** 3


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def regular(path):
    path = Path(path)
    require(not path.is_symlink() and stat.S_ISREG(path.stat().st_mode), 'Expected a regular input file: ' + str(path))
    return path


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'scripts' / filename)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def region(path, offset, length):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        stream.seek(offset)
        while length:
            body = stream.read(min(length, 8*M))
            require(bool(body), 'Short image region')
            h.update(body)
            length -= len(body)
    return h.hexdigest()


def copy_region(source, destination, offset, length, target):
    with Path(source).open('rb') as src, Path(destination).open('r+b') as dst:
        src.seek(offset)
        dst.seek(target)
        while length:
            body = src.read(min(length, 8*M))
            require(bool(body), 'Short source partition')
            if body.count(0) == len(body):
                dst.seek(len(body), 1)
            else:
                dst.write(body)
            length -= len(body)


def allocated(path):
    with Path(path).open('rb') as stream:
        offset, total = 0, Path(path).stat().st_size
        while offset < total:
            try:
                offset = os.lseek(stream.fileno(), offset, os.SEEK_DATA)
                end = os.lseek(stream.fileno(), offset, os.SEEK_HOLE)
            except OSError as exc:
                if exc.errno == errno.ENXIO:
                    return
                if exc.errno not in (errno.EINVAL, errno.ENOTSUP):
                    raise
                yield 0, total
                return
            yield offset, end-offset
            offset = end


def align_up(value, alignment):
    return ((value + alignment - 1) // alignment) * alignment


def compact_super_slices(slices_bytes, source, source_super_offset, source_super_bytes):
    """Repack only active _a EROFS slices tightly from offset 1 MiB inside super."""
    cursor = M
    plan = []
    seen = set()
    with Path(source).open('rb') as f:
        for line in slices_bytes.decode().splitlines():
            if not line.strip():
                continue
            name, old_off_s, size_s = line.split()
            old_off, size = int(old_off_s), int(size_s)
            require(name not in seen, 'Duplicate super slice: ' + name)
            seen.add(name)
            require(0 <= old_off < old_off + size <= source_super_bytes, 'Logical partition outside source super: ' + name)
            f.seek(source_super_offset + old_off + 1024)
            require(f.read(4) == b'\xe2\xe1\xf5\xe0', 'Expected EROFS logical partition: ' + name)
            new_off = align_up(cursor, M)
            plan.append((name, old_off, new_off, size))
            cursor = new_off + size
    require(plan, 'No active super slices found')
    total_super = align_up(cursor, M)
    text = ''.join(f'{name} {new_off} {size}\n' for name, _, new_off, size in plan).encode()
    return total_super, plan, text


def compute_super_sha256(source, source_super_offset, super_bytes, slice_plan=None):
    """Compute the exact SHA-256 of the output super partition (compacted or verbatim)."""
    if slice_plan is None:
        return region(source, source_super_offset, super_bytes)
    h = hashlib.sha256()
    zeros = b'\0' * M
    cursor = 0
    with Path(source).open('rb') as f:
        for _, old_off, new_off, sz in sorted(slice_plan, key=lambda item: item[2]):
            require(new_off >= cursor and new_off + sz <= super_bytes, 'Invalid super slice plan bounds')
            gap = new_off - cursor
            while gap > 0:
                chunk = min(gap, len(zeros))
                h.update(zeros[:chunk])
                gap -= chunk
            f.seek(source_super_offset + old_off)
            left = sz
            while left > 0:
                data = f.read(min(left, 8 * M))
                require(bool(data), 'Short read while hashing super slice')
                h.update(data)
                left -= len(data)
            cursor = new_off + sz
    tail = super_bytes - cursor
    while tail > 0:
        chunk = min(tail, len(zeros))
        h.update(zeros[:chunk])
        tail -= chunk
    return h.hexdigest()


def plan_minimal_layout(super_bytes, image_bytes=8*G, esp_bytes=1*G):
    """5-partition universal layout: super, metadata, misc, SQ2PROBE, userdata."""
    require(image_bytes % 512 == 0, 'Image size must be sector-aligned')
    require(super_bytes % M == 0 and super_bytes > 0, 'Unaligned compacted super')
    require(esp_bytes % M == 0 and esp_bytes >= 768*M, 'Minimal ESP must be 1 MiB aligned and >= 768 MiB')
    cursor = M
    parts = []
    for index, name, size in (
        (1, 'super', super_bytes),
        (2, 'metadata', 64*M),
        (3, 'misc', 4*M),
        (4, 'SQ2PROBE', esp_bytes),
    ):
        parts.append(dict(index=index, name=name, offset=cursor, bytes=size))
        cursor = align_up(cursor + size, M)
    data_offset = cursor
    data_bytes = ((image_bytes // 512 - 33) * 512 - data_offset) // 4096 * 4096
    require(data_bytes >= 1*G, 'Image is too small for minimal partitions, ESP and fresh userdata')
    parts.append(dict(index=5, name='userdata', offset=data_offset, bytes=data_bytes))
    return parts


def write_minimal_gpt(image, parts):
    """Write a fresh protective MBR and primary/backup GPT containing only parts."""
    count, size = 128, 128
    table = bytearray(count * size)
    esp_guid = uuid.UUID('c12a7328-f81f-11d2-ba4b-00a0c93ec93b').bytes_le
    linux_guid = uuid.UUID('0fc63daf-8483-4772-8e79-3d69d8477de4').bytes_le
    for p in parts:
        require(1 <= p['index'] <= count, 'GPT partition index out of range')
        pos = (p['index'] - 1) * size
        table[pos:pos+16] = esp_guid if p['name'] == 'SQ2PROBE' else linux_guid
        table[pos+16:pos+32] = uuid.uuid4().bytes_le
        struct.pack_into('<QQ', table, pos+32, p['offset']//512, (p['offset']+p['bytes'])//512-1)
        table[pos+56:pos+128] = p['name'].encode('utf-16le').ljust(72, b'\0')
    total = Path(image).stat().st_size // 512
    guid = uuid.uuid4().bytes_le
    def make_header(here, other, table_lba):
        h = bytearray(512)
        struct.pack_into('<8sIIIIQQQQ16sQIII', h, 0, b'EFI PART', 0x10000, 92, 0, 0,
                         here, other, 34, total-34, guid, table_lba, count, size, zlib.crc32(table))
        struct.pack_into('<I', h, 16, zlib.crc32(h[:92]))
        return h
    mbr = bytearray(512)
    mbr[446:462] = struct.pack('<B3sB3sII', 0, b'\0\2\0', 0xee, b'\xff'*3, 1, min(total-1, 0xffffffff))
    mbr[510:] = b'\x55\xaa'
    with Path(image).open('r+b') as stream:
        for offset, body in [(0, mbr), (512, make_header(1, total-1, 2)), (1024, table),
                             ((total-33)*512, table), ((total-1)*512, make_header(total-1, 1, total-33))]:
            stream.seek(offset)
            stream.write(body)


def verify_super_erofs_slices(image, super_offset, slice_plan):
    with Path(image).open('rb') as f:
        for name, _, new_off, _ in slice_plan:
            f.seek(super_offset + new_off + 1024)
            require(f.read(4) == b'\xe2\xe1\xf5\xe0', 'Compacted EROFS verification failed: ' + name)


def plan_layout(original, image_bytes, esp_bytes=2*G):
    require(image_bytes % 512 == 0, 'Image size must be sector-aligned')
    parts = {p['name']: dict(p) for p in original['partitions']}
    require(len(parts) == len(original['partitions']), 'Duplicate source partition names')
    require(not any(p['index'] == 6 for p in parts.values()), 'Source GPT slot 6 is not free')
    for name in ('super', 'userdata', 'ota_recovery_b', 'metadata', 'misc'):
        require(name in parts, 'Missing source partition: ' + name)
    parts['ota_recovery_b']['offset'] = parts['userdata']['offset']
    esp_offset = ((parts['ota_recovery_b']['offset'] + parts['ota_recovery_b']['bytes'] + M-1)//M)*M
    data_offset = esp_offset + esp_bytes
    data_bytes = ((image_bytes//512-33)*512-data_offset)//4096*4096
    require(data_bytes > 0, 'Image is too small for source partitions, ESP and fresh userdata')
    parts['userdata'].update(offset=data_offset, bytes=data_bytes)
    parts['SQ2PROBE'] = dict(index=6, name='SQ2PROBE', offset=esp_offset, bytes=esp_bytes)
    ordered = sorted(parts.values(), key=lambda p: p['index'])
    ranges = []
    for p in ordered:
        require(p['offset'] % 512 == p['bytes'] % 512 == 0 and p['bytes'] > 0, 'Unaligned partition')
        start, end = p['offset'], p['offset'] + p['bytes']
        require(34*512 <= start < end <= (image_bytes//512-33)*512, 'Partition exceeds image')
        require(all(not (start < b and a < end) for a,b in ranges), 'Overlapping partitions')
        ranges.append((start,end))
    return ordered


def write_gpt(source, image, parts):
    with Path(source).open('rb') as stream:
        stream.seek(512)
        header = stream.read(512)
        table_lba, count, size = struct.unpack_from('<QII', header, 72)
        require((count, size) == (128, 128), 'Unsupported source GPT entry format')
        stream.seek(table_lba*512)
        table = bytearray(stream.read(count*size))
    for p in parts:
        pos = (p['index']-1)*size
        if p['name'] == 'SQ2PROBE':
            table[pos:pos+size] = bytes(size)
            table[pos:pos+16] = uuid.UUID('c12a7328-f81f-11d2-ba4b-00a0c93ec93b').bytes_le
        table[pos+16:pos+32] = uuid.uuid4().bytes_le
        struct.pack_into('<QQ', table, pos+32, p['offset']//512, (p['offset']+p['bytes'])//512-1)
        table[pos+56:pos+128] = p['name'].encode('utf-16le').ljust(72,b'\0')
    total = Path(image).stat().st_size//512
    guid = uuid.uuid4().bytes_le
    def make_header(here, other, table_lba):
        h = bytearray(512)
        struct.pack_into('<8sIIIIQQQQ16sQIII', h, 0, b'EFI PART', 0x10000, 92, 0, 0,
                         here, other, 34, total-34, guid, table_lba, count, size, zlib.crc32(table))
        struct.pack_into('<I', h, 16, zlib.crc32(h[:92]))
        return h
    mbr = bytearray(512)
    mbr[446:462] = struct.pack('<B3sB3sII',0,b'\0\2\0',0xee,b'\xff'*3,1,min(total-1,0xffffffff))
    mbr[510:] = b'\x55\xaa'
    with Path(image).open('r+b') as stream:
        for offset, body in [(0,mbr),(512,make_header(1,total-1,2)),(1024,table),
                             ((total-33)*512,table),((total-1)*512,make_header(total-1,1,total-33))]:
            stream.seek(offset)
            stream.write(body)


def boot_helper(name):
    return (ROOT/'probe/test2'/name).read_bytes().replace(b'\r\n', b'\n')


def storage_payload(base, parts, super_header_sha256=None, anonymous=False, super_slices=None, super_sha256=None):
    cpio = cpio_module()
    before = cpio.entries(gzip.decompress(base))
    mode, init = before['init']
    # Other normal boot setup (HOME/default wallpaper) may run after the user
    # policy. Require one policy before handoff, not textual adjacency.
    require(init.count(HOOK) == 1 and init.count(ANCHOR) == 1 and
            init.index(HOOK) < init.index(ANCHOR) and 'bin/apply-single-user' in before,
            'Require the completed single-user parent')
    old = b'sq2-loop-slice "${ssd_part}3" "$offset" "$bytes"'
    require(init.count(old) == 1, 'Unexpected original super-device handoff')
    init = init.replace(old, b'sq2-loop-slice /dev/block/by-name/super "$offset" "$bytes"')
    # This is an old fixture serial, not target hardware identity. Do not
    # replicate it as a prerequisite or pretend every target is that machine.
    init = init.replace(b' androidboot.serialno=SQ2TEST', b'')
    # Retired USB fixture and NVMe opt-in checks must not survive in the
    # universal initramfs. Keep the explicit virtio smoke path; all physical
    # boots use the image helper's unique-disk/format/partition checks.
    legacy_storage = re.compile(
        rb"(?m)^if ! grep -q 'sq2\.storage=nvme' /proc/cmdline; then\n"
        rb"[^\n]+\nfi\nif \[ \"\$ssd\" = 1 \]; then\n"
        rb"    \. /bin/prepare-native-ssd\n"
        rb"elif \[ \"\$smoke\" = 0 \]; then\n"
        rb"(?:    [^\n]*\n)+else\n"
        rb"    mkdir -p /media/esp\n"
        rb"    mount -t vfat -o sync /dev/vdg /media/esp \|\| fail smoke-log-disk\nfi\n")
    portable_storage = b'''# All physical image boots use runtime layout discovery, not lab media identity.
if [ "$smoke" = 1 ]; then
    mkdir -p /media/esp
    mount -t vfat -o sync /dev/vdg /media/esp || fail smoke-log-disk
else
    ssd=1
    . /bin/prepare-native-ssd
fi
'''
    legacy = legacy_storage.findall(init)
    if legacy:
        require(len(legacy) == 1, 'Ambiguous legacy storage selection')
        init = legacy_storage.sub(portable_storage, init, count=1)
    else:
        require(not any(token in init for token in
                        (b'USB-size', b'USB-serial', b'ESP-UUID', b'DATA-UUID',
                         b'Internal NVMe was enumerated without authorization')),
                'Unrecognized legacy storage admission; refusing partial removal')
    # A Type-C-enabled DT binds USB to this bundled PHY, not the built-in
    # qcom provider. Load it before searching USB disks; the display-stage
    # hook runs only after the system partition has already been mounted.
    early_storage = {}
    if 'etc/spx-typec/phy-spx-qmp-combo.ko' in before:
        require('bin/spx-typec-stage' in before, 'Missing Type-C stage')
        stage_mode, stage = before['bin/spx-typec-stage']
        stage = stage.replace(b'\r\n', b'\n')
        phy_load = b'insmod /etc/spx-typec/phy-spx-qmp-combo.ko\n'
        discovery = b'    . /bin/prepare-native-ssd\n'
        require(stage.count(phy_load) == 1 and init.count(discovery) == 1,
                'Unknown early USB PHY load anchors')
        early_load = (b'    insmod /etc/spx-typec/phy-spx-qmp-combo.ko '
                      b'>> /logs/spx-typec.txt 2>&1 || fail image-usb-phy\n')
        init = init.replace(discovery, early_load + discovery, 1)
        typec_load = b'insmod /etc/spx-typec/spx-typec.ko\n'
        require(stage.count(typec_load) == 1 and
                b'parmtype=boot_storage_ports:uint' in before['etc/spx-typec/spx-typec.ko'][1],
                'Rebuild Type-C with boot-storage connector protection')
        protected_stage = boot_helper('protect-usb-boot-port')
        stage = stage.replace(typec_load, protected_stage, 1)
        # Keep the remaining SAM/UCSI/gadget setup and delayed HPD replay.
        # Do not reload or hot-swap an active PHY in the late stage.
        early_storage['bin/spx-typec-stage'] = (stage_mode, stage.replace(phy_load, b'', 1))
    if early_storage:
        init = typec_early_handoff(init, early_storage, before)
        init = optimize_boot_init(init)
        init = optional_boot_failures(init)
        init = init.replace(b'System images read-only; provisioned system disk selected; original Android init will become PID 1.', b'System images will be read-only; boot-source disk discovery pending; original Android init will become PID 1.')
    helper = boot_helper('prepare-image-ssd')
    layout = b'# Universal image; no boot-time content hash gate.\nSPX_IMAGE_FORMAT=1\n'
    partition_table = ''.join(f"{p['name']} {p['index']} {p['bytes']//512}\n" for p in parts).encode()
    delta = {'init':(mode,init), 'bin/prepare-native-ssd':(0o100755,helper),
             'etc/sq2-ssd-layout':(0o100644,layout), 'etc/sq2-ssd-partitions':(0o100644,partition_table)}
    if super_slices is not None:
        delta['etc/sq2-ssd-super-slices'] = (0o100644, super_slices)
    if super_sha256 and 'etc/spx-ota-info' in before:
        from full_image_ota import update_ota_info_super_sha256
        info_mode, info_body = before['etc/spx-ota-info']
        delta['etc/spx-ota-info'] = (info_mode, update_ota_info_super_sha256(info_body, super_sha256))
    delta.update(early_storage)
    # Optional storage preparation: notify before zygote; never gate it.
    if 'bin/surface-swap-boot' in before:
        storage_hook = b'if [ "$ssd" = 1 ]; then\n    /bin/storage-ready-stage > /logs/storage-ready-stage.txt 2>&1 || echo STORAGE_READY_STAGE_FAILED_NONFATAL\nfi\n'
        require(init.count(b'exec native-handoff /newroot\n') == 1, 'Unknown storage scheduling anchor')
        init = init.replace(b'exec native-handoff /newroot\n', storage_hook+b'exec native-handoff /newroot\n', 1)
        delta['init'] = (mode, init)
        delta['bin/surface-swap-boot'] = (0o100755, boot_helper('surface-swap-boot'))
        delta['bin/grow-image-userdata'] = (0o100755, boot_helper('grow-image-userdata'))
        delta['bin/storage-ready-stage'] = (0o100755, boot_helper('storage-ready-stage'))
    require(init.count(b'exec native-handoff /newroot\n') == 1, 'Unknown GSC stage anchor')
    init = init.replace(
        b'exec native-handoff /newroot\n',
        b'/bin/gscd-availability-stage > /logs/gscd-availability.txt 2>&1 || echo GSC_AVAILABILITY_STAGE_FAILED_NONFATAL\n'
        b'/bin/spx-service-hygiene-stage > /logs/spx-service-hygiene.txt 2>&1 || echo SPX_SERVICE_HYGIENE_STAGE_FAILED_NONFATAL\n'
        b'exec native-handoff /newroot\n',
        1,
    )
    delta['init'] = (mode, init)
    delta['bin/gscd-availability-stage'] = (0o100755, boot_helper('gscd-availability-stage'))
    delta['bin/spx-service-hygiene-stage'] = (0o100755, boot_helper('spx-service-hygiene-stage'))
    delta['bin/lab-input-posture'] = (0o100755, boot_helper('lab-input-posture'))
    delta['bin/touch-services'] = (0o100755, boot_helper('touch-services'))
    delta['bin/touch-framework-ready'] = (0o100755, boot_helper('touch-framework-ready'))
    delta['bin/touch-resume-watch'] = (0o100755, boot_helper('touch-resume-watch'))
    delta['bin/touch-calibration-boot'] = (0o100755, boot_helper('touch-calibration-boot'))
    delta['bin/refresh-iptsd-input-nodes'] = (0o100755, boot_helper('refresh-iptsd-input-nodes'))
    if 'bin/apply-device-info' in before:
        delta['bin/apply-device-info'] = (0o100755, boot_helper('apply-device-info'))
    if 'bin/spx-lower-usb' in before:
        delta['bin/spx-lower-usb'] = (0o100755, boot_helper('spx-lower-usb'))
    delta.update(early_hidl_bootstrap(before))
    from runtime_boot_policy import apply
    runtime_bodies = {name: body for name, (_,body) in before.items()}
    if 'bin/prepare-native-gpu' in runtime_bodies:
        gpu_body = runtime_bodies['bin/prepare-native-gpu'].replace(b'\r\n', b'\n')
        rtl_anchor = b'cp -a /lib/firmware/. "$dest/"\n'
        rtl_hook = (
            b'if [ -d "$target/vendor/firmware/rtl_nic" ] && [ ! -d "$dest/rtl_nic" ]; then\n'
            b'    cp -a "$target/vendor/firmware/rtl_nic" "$dest/rtl_nic"\n'
            b'fi\n'
        )
        if rtl_anchor in gpu_body and b'rtl_nic' not in gpu_body:
            runtime_bodies['bin/prepare-native-gpu'] = gpu_body.replace(rtl_anchor, rtl_anchor + rtl_hook, 1)
            delta['bin/prepare-native-gpu'] = (before['bin/prepare-native-gpu'][0], runtime_bodies['bin/prepare-native-gpu'])
    for name in apply(runtime_bodies):
        if name not in delta or name == 'bin/prepare-native-gpu':
            delta[name] = (before[name][0],runtime_bodies[name])
    for name in ['init','bin/prepare-native-ssd','bin/spx-service-hygiene-stage','bin/lab-input-posture', *early_storage]:
        subprocess.run(['sh','-n'],input=delta[name][1],check=True)
    rest, offset, insert, identity = base, 0, len(base), False
    while rest:
        decoder = zlib.decompressobj(31)
        payload = decoder.decompress(rest)
        require(decoder.eof, 'Truncated gzip parent')
        size = len(rest)-len(decoder.unused_data)
        if not decoder.unused_data and not anonymous:
            last = cpio.entries(payload)
            identity = {'etc/shadow','bin/dropbear'} <= last.keys()
            require(not (last.keys() & delta.keys()), 'Final identity overrides storage policy')
            if identity: insert=offset
        offset += size
        rest = decoder.unused_data
    raw = b''.join(cpio.entry(n,b,m) for n,(m,b) in delta.items()) + cpio.entry('TRAILER!!!',b'',0)
    raw += b'\0'*(-len(raw)%512)
    member = gzip.compress(raw,mtime=0)
    result = base[:insert]+member+base[insert:]
    after = cpio.entries(gzip.decompress(result))
    require(after == dict(before,**delta), 'Unexpected storage payload delta')
    return result, {'changed':sorted(delta), 'original_compressed_members_byte_identical':True,
                    'credential_member_last':identity, 'insert_offset':insert,
                    'delta_sha256':None, 'content_hash_validation':False,
                    'unit_uuid_and_capacity_gate_removed':True}, member



def typec_early_handoff(init, early_storage, before):
    require(b'spx_boot_handoff=1' in before['etc/spx-typec/spx-typec.ko'][1],
            'Rebuild Type-C with early event / late routing handoff')
    stage_mode, stage = early_storage['bin/spx-typec-stage']
    for module in ('ucsi_surface_hid', 'spx-ucsi-transport'):
        line = ('insmod /etc/spx-typec/'+module+'.ko\n').encode()
        require(stage.count(line) == 1, 'Missing early Type-C module '+module)
        stage = stage.replace(line, b'')
    early_storage['bin/spx-typec-stage'] = (stage_mode, stage)
    early = boot_helper('spx-typec-early')
    early_storage['bin/spx-typec-early'] = (0o100755, early)
    discovery = b'    . /bin/prepare-native-ssd\n'
    require(init.count(discovery) == 1, 'Ambiguous storage preparation')
    init = init.replace(discovery, b'    /bin/spx-typec-early >> /logs/spx-typec.txt 2>&1 || fail typec-early\n', 1)
    # Mount /media/esp BEFORE wifi-services runs lte-unit-seed (which seeds
    # /run/rmtfs/modem_fsg from /media/esp/EFI/SPX/LTE/<uuid>.bin before rmtfs
    # starts), while still running after spx-typec-early firmware handoff and
    # giving USB-C storage enumeration time during early module loads.
    pre_wifi = b'    timeout 5 hwclock -s -u >> /logs/module-load.txt 2>&1\n'
    require(init.count(pre_wifi) == 1, 'Unknown pre-wifi storage discovery boundary')
    acquire_pre_wifi = b'    if [ "$ssd" = 1 ]; then\n        . /bin/prepare-native-ssd\n    fi\n'
    init = init.replace(pre_wifi, pre_wifi + acquire_pre_wifi, 1)
    rescue = b'if [ "$rescue" = 1 ]; then\n    echo '
    require(init.count(rescue) == 1, 'Unknown pre-mount/rescue boundary')
    acquire_fallback = b'if [ "$ssd" = 1 ] && [ ! -f /run/sq2-ssd-device ]; then\n    . /bin/prepare-native-ssd\nfi\n'
    init = init.replace(rescue, acquire_fallback + rescue, 1)
    late = b'echo 1 > /sys/bus/platform/devices/surface-typec/events_enabled 2>> /logs/spx-typec.txt || echo SPX_TYPEC_EVENTS_FAILED\n'
    require(init.count(late) == 1, 'Unknown late firmware enable')
    init = init.replace(late, b'echo 1 > /sys/bus/platform/devices/surface-typec/routing_ready 2>> /logs/spx-typec.txt || fail typec-routing\n', 1)
    # Preserve early handoff evidence rather than truncating it at the late stage.
    init = init.replace(b'/bin/spx-typec-stage > /logs/spx-typec.txt 2>&1 || echo SPX_TYPEC_STAGE_FAILED',
                        b'/bin/spx-typec-stage >> /logs/spx-typec.txt 2>&1 || fail typec-storage-protection')
    return init


def optimize_boot_init(init):
    # Called only after Type-C early handoff has been rendered.
    duplicate = b'hid_multitouch surface_aggregator surface_aggregator_registry surface_aggregator_hub surface_hid_core surface_hid'
    require(init.count(duplicate) == 1, 'Unknown common-module load group')
    init = init.replace(duplicate, b'hid_multitouch surface_hid', 1)
    cache = b'rm -f /lib/modules/"$(uname -r)"/modules.dep.bb\n'
    require(init.count(cache) == 1, 'Unknown module-cache initialization')
    init = init.replace(cache, b'', 1)
    early = b'# All physical image boots use runtime layout discovery, not lab media identity.\n'
    require(init.count(early) == 1, 'Unknown earliest module load boundary')
    init = init.replace(early, b'# Invalidate the inherited cache before the first modprobe, not after it.\n'+cache+early, 1)
    no_op = b'if grep -qw "sq2.el2=1" /proc/cmdline; then\n el2-terminal-autostart > /logs/el2-terminal-autostart.txt 2>&1 &\nfi\n'
    require(init.count(no_op) == 1, 'Unknown EL2 autostart caller')
    init = init.replace(no_op, b'# Default EL2 has no Terminal UI task; explicit lab mode retains it.\ncase " $boot_cmdline " in *" sq2.el2=experimental "*)\n el2-terminal-autostart > /logs/el2-terminal-autostart.txt 2>&1 &\n;; esac\n', 1)
    flag_lines = [(b"grep -q 'sq2.native=smoke' /proc/cmdline && smoke=1", b'case " $boot_cmdline " in *" sq2.native=smoke "*) smoke=1;; esac'),
                  (b"grep -q 'sq2.native=ssd' /proc/cmdline && ssd=1", b'case " $boot_cmdline " in *" sq2.native=ssd "*|*" sq2.native=ssd-smoke "*) ssd=1;; esac'),
                  (b"grep -q 'sq2.native=ssd-smoke' /proc/cmdline && ssd_smoke=1", b'case " $boot_cmdline " in *" sq2.native=ssd-smoke "*) ssd_smoke=1;; esac'),
                  (b"grep -q 'sq2.rescue=1' /proc/cmdline && rescue=1", b'case " $boot_cmdline " in *" sq2.rescue=1 "*) rescue=1;; esac'),
                  (b"grep -q 'sq2.debug=1' /proc/cmdline && debug=1", b'case " $boot_cmdline " in *" sq2.debug=1 "*) debug=1;; esac')]
    require(init.count(b'smoke=0\n') == 2, 'Unknown boot flag declarations')
    init = init.replace(b'smoke=0\n', b'read -r boot_cmdline < /proc/cmdline\nsmoke=0\n', 1)
    for old,new in flag_lines:
        require(init.count(old) == 1, 'Unknown boot flag parser')
        init = init.replace(old,new,1)
    return init


def optional_boot_failures(init):
    old=b'/bin/spx-typec-stage >> /logs/spx-typec.txt 2>&1 || fail typec-storage-protection\n'
    new=b'typec_routing_allowed=0\nif /bin/spx-typec-stage >> /logs/spx-typec.txt 2>&1; then\n    typec_routing_allowed=1\nelse\n    echo TYPEC_OPTIONAL_ROUTING_SKIPPED_STORAGE_MASK_NOT_READY\nfi\n'
    require(init.count(old)==1,'Unknown Type-C protection stage')
    init=init.replace(old,new,1)
    old=b'echo 1 > /sys/bus/platform/devices/surface-typec/routing_ready 2>> /logs/spx-typec.txt || fail typec-routing\n'
    new=b'if [ "$typec_routing_allowed" = 1 ]; then\n    echo 1 > /sys/bus/platform/devices/surface-typec/routing_ready 2>> /logs/spx-typec.txt || echo TYPEC_OPTIONAL_ROUTING_FAILED_NONFATAL\nfi\n'
    require(init.count(old)==1,'Unknown late optional routing stage')
    init=init.replace(old,new,1)
    for name,marker in [('el2-terminal-stage','EL2_TERMINAL_STAGE_FAILED_NONFATAL'),('radio-stage','RADIO_STAGE_FAILED_NONFATAL')]:
        old=('|| fail '+name+'\n').encode()
        require(init.count(old)==1,'Unknown optional stage '+name)
        init=init.replace(old,('|| echo '+marker+'\n').encode(),1)
    return init


def install_boot_source(tree, shim):
    """Wrap the Linux EFI stub on the already selected volume; keep EL1/EL2."""
    tool = module('boot_source_pe', 'prepare-sq2-618-debug-entry.py')
    data = regular(shim).read_bytes()
    # New objcopy-generated PE files have a valid nonzero checksum; the
    # legacy constant-patch validator requires zero only for old launchers.
    require(data[:2] == b'MZ', 'Boot-source EFI DOS header missing')
    pe = struct.unpack_from('<I', data, 60)[0]
    require(data[pe:pe+4] == b'PE\0\0' and struct.unpack_from('<H', data, pe+4)[0] == 0xaa64,
            'Boot-source EFI must be AArch64 PE')
    require(struct.unpack_from('<H', data, pe+24)[0] == 0x20b and
            struct.unpack_from('<II', data, pe+24+112+32) == (0,0), 'Unsigned PE32+ shim required')
    for directory in ('SQ2Nat', 'SQ2Res'):
        root = tree/'EFI'/directory
        for launcher in ('el1launch.efi', 'el2launch.efi'):
            path = root/launcher
            if launcher == 'el1launch.efi' and not path.exists():
                continue
            source = path.read_bytes();tool.unsigned_arm64_pe(source)
            old = ('\\EFI\\'+directory+'\\Image').encode('utf-16le')+b'\0\0'
            new = ('\\EFI\\'+directory+'\\Boot').encode('utf-16le')+b'\0\0'
            require(source.count(old)==1, 'Unknown kernel entry '+str(path))
            changed = source.replace(old,new+b'\0'*(len(old)-len(new)),1)
            require(len(source)==len(changed), 'EFI layout changed')
            path.write_bytes(changed)
        (root/'Boot').write_bytes(data)



def early_hidl_bootstrap(before):
    """The real HIDL manager starts before default-namespace APEX activation."""
    import tempfile
    names = ('etc/sq2-hidl/hwservicemanager',
             'etc/sq2-qti-system/hidl-support/hwservicemanager')
    available = [n for n in names if n in before]
    if not available:
        return {}
    bodies = {before[n][1] for n in available}
    require(len(bodies)==1, 'Conflicting early HIDL managers')
    payload = bodies.pop()
    expected = '/system/bin/bootstrap/linker64'
    with tempfile.TemporaryDirectory() as td:
        path = Path(td)/'hwservicemanager'
        path.write_bytes(payload)
        interp = subprocess.check_output(['patchelf','--print-interpreter',path], text=True).strip()
        require(interp in (expected, '/system/bin/linker64'), 'Unexpected HIDL interpreter')
        if interp != expected:
            subprocess.run(['patchelf','--page-size','16384','--set-interpreter',expected,path],check=True)
        require(subprocess.check_output(['patchelf','--print-interpreter',path],text=True).strip()==expected,
                'Bootstrap HIDL interpreter not installed')
        result = path.read_bytes()
    return {n: (before[n][0], result) for n in available}

def hardware_panel_profiles(tree):
    import libfdt
    for directory in ('SQ2Nat','SQ2Res'):
        for name,model in [('android-sq1.dtb','SQ1'),('android-el2.dtb','SQ2')]:
            path=tree/'EFI'/directory/name
            if name == 'android-sq1.dtb' and not path.exists():
                continue
            f=libfdt.Fdt(path.read_bytes());f.resize(len(f.as_bytearray())+2048)
            f.setprop(f.path_offset('/'),'model',('Microsoft Surface Pro X ('+model+')\0').encode())
            f.setprop(f.path_offset('/chosen'),'microsoft,boot-profile',(model+'\0').encode())
            panel=f.path_offset('/soc@0/display-subsystem@ae00000/displayport-controller@ae9a000/aux-bus/panel')
            f.setprop(panel,'microsoft,preserve-boot-panel-power',b'')
            f.pack();path.write_bytes(bytes(f.as_bytearray()))

def shared_model_router(router, directory):
    """Retain SMBIOS detection; point both supported models at this shared payload.

    SQ2Nat is a historical directory name, not a hardware admission rule. The
    private router still has its original SQ1Default path; that directory is not
    part of a fresh shared 6.18 image. Fix the path, never spoof the model.
    """
    tool = module('shared_router_pe','prepare-sq2-618-debug-entry.py')
    tool.unsigned_arm64_pe(router)
    old = '\\EFI\\SQ1Default\\el2launch.efi'
    new = '\\EFI\\'+directory+'\\el2launch.efi'
    before = old.encode('utf-16le')+b'\0\0'
    after = new.encode('utf-16le')+b'\0\0'
    require(router.count(before)==1 and len(after)<=len(before), 'Unexpected SQ1 shared-payload route')
    result=router.replace(before,after+b'\0'*(len(before)-len(after)))
    require(result.count(after)==2,'Both model branches must name the existing shared launcher')
    return result


def rescue_efi(tree, efi, graphics_argument='sq2.vulkan=qcom'):
    tool = module('disk_efi','prepare-sq2-618-debug-entry.py')
    source = tree/'EFI/SQ2Nat'
    dest = tree/'EFI/SQ2Res'
    dest.mkdir(exist_ok=True)
    for path in source.iterdir():
        if path.name not in ('SHA256SUMS',): shutil.copyfile(path,dest/path.name)
    router,_ = tool.rewrite_efi((source/'start.efi').read_bytes(),'SQ2Nat','SQ2Res',1,graphics_argument)
    launcher,_ = tool.rewrite_efi((source/'el2launch.efi').read_bytes(),'SQ2Nat','SQ2Res',5,graphics_argument)
    command = (source/'kernel-options.txt').read_text().strip().replace('\\EFI\\SQ2Nat\\','\\EFI\\SQ2Res\\')
    rescue = command.replace('sq2.video.c2=experimental','sq2.rescue=1')
    require(rescue != command and len(rescue) <= len(command), 'Unsupported rescue options')
    old = command.encode('utf-16le')+b'\0\0'
    new = rescue.encode('utf-16le')+b'\0\0'
    require(launcher.count(old)==1,'Embedded rescue command mismatch')
    launcher = launcher.replace(old,new+b'\0'*(len(old)-len(new)))
    (dest/'start.efi').write_bytes(shared_model_router(router,'SQ2Res'))
    (source/'start.efi').write_bytes(shared_model_router((source/'start.efi').read_bytes(),'SQ2Nat'))
    (dest/'el2launch.efi').write_bytes(launcher)
    (dest/'kernel-options.txt').write_text(rescue+'\n')
    tool.inspect_efi((dest/'start.efi').read_bytes(),launcher,rescue+'\n','SQ2Res',graphics_argument,router_target_count=2)
    tool.inspect_efi((source/'start.efi').read_bytes(),(source/'el2launch.efi').read_bytes(),
                     (source/'kernel-options.txt').read_text(),'SQ2Nat',graphics_argument,router_target_count=2)
    # These are fresh generated bundles, not checksum-pinned release packages.
    for directory in [source,dest]:
        (directory/'SHA256SUMS').unlink(missing_ok=True)


def build(args):
    require(os.name=='posix','Use Linux/WSL for filesystem image tools')
    output = args.output.resolve()
    build_only = args.build_only
    resume = args.resume_build
    require(not output.is_symlink() and (not output.exists() or resume),'Output directory must be fresh (or explicitly resume this build)')
    template = getattr(args, 'template', None)
    candidate = regular(args.single_user_initramfs)
    recovery = regular(args.recovery_zip) if not template else None
    resources = args.resources.resolve()
    selected = policy()
    google = json.loads((ROOT/'configs/sources.lock.json').read_text())['googlebook']
    input_root = getattr(args, 'private_root', None) or resources/'private/build-inputs'
    bundle = getattr(args, 'prepared_bundle', None) or input_root/'kernel-6.18/EFI/SQ2Nat'
    if not getattr(args, 'prepared_bundle', None):
        for name in selected['files']:
            regular(bundle/name)
    manifest=json.loads((resources/'RESOURCE-MANIFEST.json').read_text())
    for name in manifest['efi_external_firmware']:
        regular(input_root/'kernel-6.18/firmware'/name)
    bootloader=input_root/'kernel-6.18/bootloader/BOOTAA64.EFI'
    regular(bootloader)
    output.mkdir(parents=True,exist_ok=resume)
    work=output/'work';work.mkdir(exist_ok=resume)
    records=[]
    def run(cmd):
        cmd=list(map(str,cmd));p=subprocess.run(cmd,capture_output=True,text=True)
        records.append({'command':cmd,'stdout':p.stdout,'stderr':p.stderr,'exit':p.returncode})
        (output/'COMMANDS.json').write_text(json.dumps(records,indent=2)+'\n')
        require(p.returncode==0,'Tool failed; see COMMANDS.json: '+cmd[0])
        return p.stdout
    source=work/'mica-original.raw'
    if template:
        source = regular(template)
        print('USE_ORIGINAL_TEMPLATE ' + str(source), flush=True)
    elif resume and source.exists() and (output/'LAYOUT.json').is_file():
        regular(source)
        print('REUSE_COMPLETED_PRISTINE_RECOVERY_EXTRACTION',flush=True)
    else:
        print('LOAD_RECOVERY_ZIP',flush=True)
        with zipfile.ZipFile(recovery) as archive:
            images=[m for m in archive.infolist() if m.filename.endswith(('.img','.bin','.raw')) and m.file_size>G]
            require(len(images)==1,'Expected one raw recovery disk in ZIP')
            with archive.open(images[0]) as src,source.open('xb') as dst:
                shutil.copyfileobj(src,dst,8*M)
    gpt=module('disk_gpt','inspect-recovery.py')
    original=gpt.inspect(source)
    original_map={p['name']:p for p in original['partitions']}
    base=candidate.read_bytes()
    c=cpio_module();before=c.entries(gzip.decompress(base))
    minimal_partitions=bool(getattr(args,'minimal_partitions',False))
    slice_plan=None
    super_slices_override=None
    if minimal_partitions:
        compacted_super,slice_plan,super_slices_override=compact_super_slices(
            before['etc/sq2-ssd-super-slices'][1],source,original_map['super']['offset'],original_map['super']['bytes'])
        image_bytes=(args.size_gib*G) if getattr(args,'size_gib',None) else 8*G
        esp_bytes=(args.esp_mib*M) if getattr(args,'esp_mib',None) else 1*G
        parts=plan_minimal_layout(compacted_super,image_bytes=image_bytes,esp_bytes=esp_bytes)
        mapping={p['name']:p for p in parts}
    else:
        image_bytes = original['image_bytes'] if template else args.size_gib*G
        parts = original['partitions'] if template else plan_layout(original,image_bytes)
        mapping={p['name']:p for p in parts}
        for line in before['etc/sq2-ssd-super-slices'][1].decode().splitlines():
            name,offset,size=line.split();offset,size=int(offset),int(size)
            require(0<=offset<offset+size<=mapping['super']['bytes'],'Logical partition outside super')
            with source.open('rb') as f:
                f.seek(original_map['super']['offset']+offset+1024)
                require(f.read(4)==b'\xe2\xe1\xf5\xe0','Expected EROFS logical partition: '+name)
    super_sha256 = compute_super_sha256(source, original_map['super']['offset'], mapping['super']['bytes'], slice_plan=slice_plan)
    payload,audit,member=storage_payload(base,parts,anonymous=getattr(args, "anonymous", False),super_slices=super_slices_override,super_sha256=super_sha256)
    (output/'storage-layout-delta.cpio.gz').write_bytes(member)
    tree=work/'esp-tree';(tree/'EFI/BOOT').mkdir(parents=True,exist_ok=resume);(tree/'EFI/SQ2Nat').mkdir(exist_ok=resume)
    for name in selected['files']:
        shutil.copyfile(bundle/name,tree/'EFI/SQ2Nat'/name)
    require(b'microsoft,preserve-boot-panel-power\0' in (tree/'EFI/SQ2Nat/Image').read_bytes(),
            'Portable boot requires a kernel built with the panel power handoff patch')
    (tree/'EFI/SQ2Nat/initramfs.cpio.gz').write_bytes(payload)
    shutil.copyfile(bootloader,tree/'EFI/BOOT/BOOTAA64.EFI')
    shutil.copytree(input_root/'kernel-6.18/firmware',tree/'firmware',dirs_exist_ok=resume)
    for name,data in getattr(args, 'additional_firmware', {}).items():
        relative=Path(name)
        require(not relative.is_absolute() and '..' not in relative.parts, 'Invalid firmware path')
        target=tree/'firmware'/relative
        target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes(data)
    graphics_argument = module('disk_graphics','prepare-sq2-618-debug-entry.py').graphics_option(
        (bundle/'kernel-options.txt').read_text())
    rescue_efi(tree,bundle,graphics_argument)
    from full_image_boot_source import build as build_boot_source
    shim = build_boot_source(work/'boot-source', getattr(args, 'boot_gnu_efi', None) or
                             input_root/'kernel-6.18/gnu-efi', run)
    if template:
        # Shared binaries do not mean identical frequency/power policies. Keep
        # the original universal template's SQ1 selection; SQ2 remains intact.
        from full_image_models import from_template
        from_template(tree,source,original_map['SQ2PROBE']['offset'],
                      (bundle/'start.efi').read_bytes(),work,graphics_argument)
    # Model-profile installation creates el1launch.efi from el2launch.efi.
    # Wrap only after both model-specific launchers have been finalized.
    install_boot_source(tree, Path(shim))
    hardware_panel_profiles(tree)
    (tree/'SPX-IMAGE.txt').write_text('SPX_IMAGE_FORMAT=1\nSPX_RELEASE=6.18-'+selected['baseline']+'-single-user\nPRIVATE_IDENTITY_PRESENT='+('0' if getattr(args, 'anonymous', False) else '1')+'\n')
    (tree/'EFI/BOOT/grub.cfg').write_text("set timeout=5\nset default=spx-single-user\n\nmenuentry 'Surface Pro X Linux 6.18 - single user 0' --id spx-single-user {\n chainloader /EFI/SQ2Nat/start.efi\n}\nmenuentry 'Surface Pro X Linux 6.18 - rescue (SSH / Wi-Fi / USB)' --id spx-rescue {\n chainloader /EFI/SQ2Res/start.efi\n}\nmenuentry 'Power off' { halt }\n")
    esp_only = bool(getattr(args, 'esp_only', False))
    (output/'LAYOUT.json').write_text(json.dumps({'image_bytes':image_bytes,'partitions':parts,'template':str(template) if template else None,'partition_boundaries_preserved':bool(template) and not minimal_partitions,'minimal_partitions':minimal_partitions},indent=2)+'\n')
    files={p.relative_to(tree).as_posix():{'bytes':p.stat().st_size} for p in tree.rglob('*') if p.is_file()}
    if esp_only:
        esp_export = output/'esp'
        shutil.copytree(tree, esp_export, dirs_exist_ok=resume)
        report={'operation':'private-esp-payload-only','image':None,'image_bytes':None,
                'image_sha256':None,'compressed_image':None,'compressed_bytes':None,
                'compressed_sha256':None,'gzip_full_stream_verified':False,'gpt_primary_backup_verified':False,
                'esp_only':True,'esp_tree':str(esp_export),
                'source_private_baseline':selected['baseline'],'single_user_parent_sha256':None,
                'template':str(template) if template else None,'template_layout_preserved':bool(template) and not minimal_partitions,
                'minimal_partitions':minimal_partitions,
                'kernel_mode':getattr(args,'kernel_mode','prebuilt'),
                'final_initramfs_sha256':None,'super_sha256':super_sha256,'storage_payload':audit,
                'efi_files':files,'content_hash_validation':False,'build_only_requested':build_only,
                'new_rollback_performed':False,'device_written':False,'hardware_boot_tested':False,
                'private_identity_present':not getattr(args, 'anonymous', False),'anonymous_public_image':False,'userdata_migrated':False,
                'hardware_scope':'ESP-only candidate with native-SSD storage delta; boot/SQ1/SQ2 untested'}
        (output/'IMAGE-MANIFEST.json').write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps({k:report[k] for k in ['esp_only','esp_tree','hardware_boot_tested']},indent=2),flush=True)
        return report
    fat=work/'esp.fat'
    if fat.exists(): regular(fat)
    with fat.open('wb' if resume else 'xb') as stream:stream.truncate(mapping['SQ2PROBE']['bytes'])
    run(['/usr/sbin/mkfs.fat','-F','32','-n','SQ2PROBE',fat])
    run(['mcopy','-i',fat,'-s',*[str(p) for p in tree.iterdir()],'::/'])
    if not build_only:
        run(['/usr/sbin/fsck.fat','-n',fat])
        readback=work/'esp-readback';readback.mkdir(exist_ok=resume)
        run(['mcopy','-i',fat,'-s','::/*',readback])
        expected = {p.relative_to(tree).as_posix(): sha(p) for p in tree.rglob('*') if p.is_file()}
        actual = {p.relative_to(readback).as_posix(): sha(p) for p in readback.rglob('*') if p.is_file()}
        require(actual == expected, 'ESP readback differs from assembled payload')
        for name, value in expected.items(): files[name]['sha256'] = value
    image=output/('surface-pro-x-6.18-hybrid-generic.img' if getattr(args, 'anonymous', False) else 'surface-pro-x-6.18-single-user-private.img')
    if image.exists(): regular(image)
    with image.open('wb' if resume else 'xb') as stream:stream.truncate(image_bytes)
    partition_hashes={}
    if minimal_partitions:
        src_super = original_map['super']['offset']
        dst_super = mapping['super']['offset']
        for name, old_off, new_off, sz in slice_plan:
            print('COPY_COMPACT_SUPER_SLICE', name, flush=True)
            copy_region(source, image, src_super + old_off, sz, dst_super + new_off)
    else:
        for p in parts:
            if p['name'] in ('userdata','metadata','misc','SQ2PROBE'):continue
            if getattr(args, 'anonymous', False) and p['name'] in ('persist', 'frp_persist', 'rawdump', 'desktop_security_persist', 'desktop_security_storage'):
                continue
            src=original_map[p['name']]
            print('COPY_PRISTINE_PARTITION',p['name'],flush=True)
            copy_region(source,image,src['offset'],src['bytes'],p['offset'])
    fresh={}
    for name in ('userdata','metadata'):
        if build_only:
            features=['-O','extent,64bit,metadata_csum,casefold,encrypt,verity'] if name=='userdata' else []
            extended=f"offset={mapping[name]['offset']},lazy_itable_init=0,lazy_journal_init=0"
            if name=='userdata': extended+=',encoding=utf8'
            run(['/usr/sbin/mke2fs','-q','-F','-t','ext4','-b','4096','-m','0','-L','SPXDATA' if name=='userdata' else 'SPXMETA','-U',str(uuid.uuid4()),*features,'-E',extended,image,str(mapping[name]['bytes']//4096)])
            fresh[name]={'bytes':mapping[name]['bytes'],'created_fresh':True,'readback_performed':False}
            continue
        fs=work/(name+'.ext4')
        with fs.open('xb') as stream:stream.truncate(mapping[name]['bytes'])
        options=['-O','extent,64bit,metadata_csum,casefold,encrypt,verity','-E','encoding=utf8,lazy_itable_init=0,lazy_journal_init=0'] if name=='userdata' else ['-E','lazy_itable_init=0,lazy_journal_init=0']
        run(['/usr/sbin/mke2fs','-q','-F','-t','ext4','-b','4096','-m','0','-L','SPXDATA' if name=='userdata' else 'SPXMETA','-U',str(uuid.uuid4()),*options,fs])
        run(['/usr/sbin/e2fsck','-fn',fs])
        listing=run(['/usr/sbin/debugfs','-R','ls -l /',fs])
        require(all(s in ('.','..','lost+found') for s in [line.split()[-1] for line in listing.splitlines() if line.strip()]),'Fresh filesystem contains unexpected files')
        for offset,size in allocated(fs):copy_region(fs,image,offset,size,mapping[name]['offset']+offset)
        fresh[name]={'bytes':mapping[name]['bytes'],'root_listing':listing,'empty_except_lost_found':True}
    for offset,size in allocated(fat):copy_region(fat,image,offset,size,mapping['SQ2PROBE']['offset']+offset)
    if minimal_partitions:
        write_minimal_gpt(image, parts)
    elif template:
        # Preserve the template's protective MBR, both GPTs, attributes and GUIDs.
        # No guessed capacity, reordered partitions or new layout is introduced.
        first = 34*512 if getattr(args, 'anonymous', False) else min(p['offset'] for p in parts)
        last = image_bytes-33*512 if getattr(args, 'anonymous', False) else max(p['offset'] + p['bytes'] for p in parts)
        copy_region(source,image,0,first,0)
        copy_region(source,image,last,image_bytes-last,last)
    else:
        write_gpt(source,image,parts)
    if not build_only:
        run(['/usr/sbin/sgdisk','-v',image])
        inspected=gpt.inspect(image)
        require([(p['index'],p['name'],p['offset'],p['bytes']) for p in inspected['partitions']]==[(p['index'],p['name'],p['offset'],p['bytes']) for p in parts],'Final GPT differs from plan')
        if minimal_partitions:
            verify_super_erofs_slices(image, mapping['super']['offset'], slice_plan)
    compressed=image.with_suffix('.img.gz')
    print('COMPRESS_FULL_IMAGE',flush=True)
    if compressed.exists(): regular(compressed)
    with image.open('rb') as src,compressed.open('wb' if resume else 'xb') as dst:
        process=subprocess.Popen(['pigz','-1','-n','-p',str(args.jobs)],stdin=subprocess.PIPE,stdout=dst,stderr=subprocess.PIPE)
        while body:=src.read(8*M):
            process.stdin.write(body)
        process.stdin.close();error=process.stderr.read().decode();status=process.wait()
        require(status==0,'pigz failed: '+error)
    raw_sha = compressed_sha = None
    zeroed = []
    if not build_only:
        raw_sha, compressed_sha = sha(image), sha(compressed)
        h = hashlib.sha256(); count = 0
        with gzip.open(compressed, 'rb') as stream:
            while block := stream.read(8*M):
                h.update(block); count += len(block)
        require(count == image_bytes and h.hexdigest() == raw_sha, 'Full gzip stream differs from image')
        require(region(image, mapping['SQ2PROBE']['offset'], mapping['SQ2PROBE']['bytes']) == sha(fat),
                'Final image ESP differs from verified FAT')
        if getattr(args, 'anonymous', False):
            with image.open('rb') as stream:
                for name in ('misc','persist','frp_persist','rawdump','desktop_security_persist','desktop_security_storage'):
                    if name not in mapping: continue
                    part = mapping[name]; stream.seek(part['offset']); left = part['bytes']
                    while left:
                        block = stream.read(min(left, 8*M))
                        require(block and not any(block), 'Nonzero private-state partition: '+name)
                        left -= len(block)
                    zeroed.append(name)
    report={'operation':'private-full-gpt-disk-image','image':str(image),'image_bytes':image.stat().st_size,
            'image_sha256':raw_sha,'compressed_image':str(compressed),'compressed_bytes':compressed.stat().st_size,
            'compressed_sha256':compressed_sha,'gzip_full_stream_verified':not build_only,'gpt_primary_backup_verified':not build_only,
            'source_recovery_zip_sha256':None, 'source_recovery_raw_sha256':None,
            'source_private_baseline':selected['baseline'],'single_user_parent_sha256':None,
            'template':str(template) if template else None,'template_layout_preserved':bool(template) and not minimal_partitions,
            'minimal_partitions':minimal_partitions,
            'kernel_mode':getattr(args,'kernel_mode','prebuilt'),
            'final_initramfs_sha256':hashlib.sha256(payload).hexdigest(),'super_sha256':super_sha256,'storage_payload':audit,
            'pristine_partition_sha256':partition_hashes,'fresh_filesystems':fresh,'efi_files':files,
            'all_efi_files_readback_verified':not build_only,'content_hash_validation':not build_only,'zeroed_identity_partitions':zeroed,'build_only_requested':build_only,'new_rollback_performed':False,'device_written':False,'hardware_boot_tested':False,
            'private_identity_present':not getattr(args, 'anonymous', False),'anonymous_public_image':False,'userdata_migrated':False,
            'hardware_scope':('New source-compiled kernel candidate with accepted SQ2 userspace inputs; boot/user0/SQ1 untested'
                              if getattr(args,'kernel_mode','prebuilt')=='source' else
                              'SQ2 accepted runtime inputs; new disk boot, user-0 applications and SQ1 remain untested')}
    (output/'IMAGE-MANIFEST.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ['image','image_bytes','image_sha256','compressed_image','compressed_bytes','compressed_sha256','final_initramfs_sha256','hardware_boot_tested']},indent=2),flush=True)
    return report
