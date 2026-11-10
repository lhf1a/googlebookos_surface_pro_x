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


def storage_payload(base, parts, super_header_sha256=None):
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
    helper = (ROOT / 'probe/test2/prepare-image-ssd').read_bytes().replace(b'\r\n',b'\n')
    layout = b'# Universal image; no boot-time content hash gate.\nSPX_IMAGE_FORMAT=1\n'
    partition_table = ''.join(f"{p['name']} {p['index']} {p['bytes']//512}\n" for p in parts).encode()
    delta = {'init':(mode,init), 'bin/prepare-native-ssd':(0o100755,helper),
             'etc/sq2-ssd-layout':(0o100644,layout), 'etc/sq2-ssd-partitions':(0o100644,partition_table)}
    from runtime_boot_policy import apply
    runtime_bodies = {name: body for name, (_,body) in before.items()}
    for name in apply(runtime_bodies):
        if name not in delta:
            delta[name] = (before[name][0],runtime_bodies[name])
    for name in ['init','bin/prepare-native-ssd']:
        subprocess.run(['sh','-n'],input=delta[name][1],check=True)
    rest, offset, insert, identity = base, 0, len(base), False
    while rest:
        decoder = zlib.decompressobj(31)
        payload = decoder.decompress(rest)
        require(decoder.eof, 'Truncated gzip parent')
        size = len(rest)-len(decoder.unused_data)
        if not decoder.unused_data:
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


def rescue_efi(tree, efi):
    tool = module('disk_efi','prepare-sq2-618-debug-entry.py')
    source = tree/'EFI/SQ2Nat'
    dest = tree/'EFI/SQ2Res'
    dest.mkdir(exist_ok=True)
    for path in source.iterdir():
        if path.name not in ('SHA256SUMS',): shutil.copyfile(path,dest/path.name)
    router,_ = tool.rewrite_efi((source/'start.efi').read_bytes(),'SQ2Nat','SQ2Res',1,'sq2.vulkan=qcom')
    launcher,_ = tool.rewrite_efi((source/'el2launch.efi').read_bytes(),'SQ2Nat','SQ2Res',5,'sq2.vulkan=qcom')
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
    tool.inspect_efi((dest/'start.efi').read_bytes(),launcher,rescue+'\n','SQ2Res','sq2.vulkan=qcom',router_target_count=2)
    tool.inspect_efi((source/'start.efi').read_bytes(),(source/'el2launch.efi').read_bytes(),
                     (source/'kernel-options.txt').read_text(),'SQ2Nat','sq2.vulkan=qcom',router_target_count=2)
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
    image_bytes = original['image_bytes'] if template else args.size_gib*G
    parts = original['partitions'] if template else plan_layout(original,image_bytes)
    mapping={p['name']:p for p in parts}
    original_map={p['name']:p for p in original['partitions']}
    base=candidate.read_bytes()
    c=cpio_module();before=c.entries(gzip.decompress(base))
    for line in before['etc/sq2-ssd-super-slices'][1].decode().splitlines():
        name,offset,size=line.split();offset,size=int(offset),int(size)
        require(0<=offset<offset+size<=mapping['super']['bytes'],'Logical partition outside super')
        with source.open('rb') as f:
            f.seek(original_map['super']['offset']+offset+1024)
            require(f.read(4)==b'\xe2\xe1\xf5\xe0','Expected EROFS logical partition: '+name)
    payload,audit,member=storage_payload(base,parts)
    (output/'storage-layout-delta.cpio.gz').write_bytes(member)
    tree=work/'esp-tree';(tree/'EFI/BOOT').mkdir(parents=True,exist_ok=resume);(tree/'EFI/SQ2Nat').mkdir(exist_ok=resume)
    for name in selected['files']:
        shutil.copyfile(bundle/name,tree/'EFI/SQ2Nat'/name)
    (tree/'EFI/SQ2Nat/initramfs.cpio.gz').write_bytes(payload)
    shutil.copyfile(bootloader,tree/'EFI/BOOT/BOOTAA64.EFI')
    shutil.copytree(input_root/'kernel-6.18/firmware',tree/'firmware',dirs_exist_ok=resume)
    rescue_efi(tree,bundle)
    if template:
        # Shared binaries do not mean identical frequency/power policies. Keep
        # the original universal template's SQ1 selection; SQ2 remains intact.
        from full_image_models import from_template
        from_template(tree,source,original_map['SQ2PROBE']['offset'],
                      (bundle/'start.efi').read_bytes(),work)
    (tree/'SPX-IMAGE.txt').write_text('SPX_IMAGE_FORMAT=1\nSPX_RELEASE=6.18-'+selected['baseline']+'-single-user\nPRIVATE_IDENTITY_PRESENT=1\n')
    (tree/'EFI/BOOT/grub.cfg').write_text("set timeout=5\nset default=spx-single-user\n\nmenuentry 'Surface Pro X Linux 6.18 - single user 0' --id spx-single-user {\n chainloader /EFI/SQ2Nat/start.efi\n}\nmenuentry 'Surface Pro X Linux 6.18 - rescue (no Android data mount)' --id spx-rescue {\n chainloader /EFI/SQ2Res/start.efi\n}\nmenuentry 'Power off' { halt }\n")
    (output/'LAYOUT.json').write_text(json.dumps({'image_bytes':image_bytes,'partitions':parts,'template':str(template) if template else None,'partition_boundaries_preserved':bool(template)},indent=2)+'\n')
    files={p.relative_to(tree).as_posix():{'bytes':p.stat().st_size} for p in tree.rglob('*') if p.is_file()}
    fat=work/'esp.fat'
    if fat.exists(): regular(fat)
    with fat.open('wb' if resume else 'xb') as stream:stream.truncate(mapping['SQ2PROBE']['bytes'])
    run(['/usr/sbin/mkfs.fat','-F','32','-n','SQ2PROBE',fat])
    run(['mcopy','-i',fat,'-s',*[str(p) for p in tree.iterdir()],'::/'])
    if not build_only:
        run(['/usr/sbin/fsck.fat','-n',fat])
        readback=work/'esp-readback';readback.mkdir(exist_ok=resume)
        run(['mcopy','-i',fat,'-s','::/*',readback])
    image=output/'surface-pro-x-6.18-single-user-private.img'
    if image.exists(): regular(image)
    with image.open('wb' if resume else 'xb') as stream:stream.truncate(image_bytes)
    partition_hashes={}
    for p in parts:
        if p['name'] in ('userdata','metadata','misc','SQ2PROBE'):continue
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
    if template:
        # Preserve the template's protective MBR, both GPTs, attributes and GUIDs.
        # No guessed capacity, reordered partitions or new layout is introduced.
        first = min(p['offset'] for p in parts)
        last = max(p['offset'] + p['bytes'] for p in parts)
        copy_region(source,image,0,first,0)
        copy_region(source,image,last,image_bytes-last,last)
    else:
        write_gpt(source,image,parts)
    if not build_only:
        run(['/usr/sbin/sgdisk','-v',image])
        inspected=gpt.inspect(image)
        require([(p['index'],p['name'],p['offset'],p['bytes']) for p in inspected['partitions']]==[(p['index'],p['name'],p['offset'],p['bytes']) for p in parts],'Final GPT differs from plan')
    compressed=image.with_suffix('.img.gz')
    print('COMPRESS_FULL_IMAGE',flush=True)
    if compressed.exists(): regular(compressed)
    with image.open('rb') as src,compressed.open('wb' if resume else 'xb') as dst:
        process=subprocess.Popen(['pigz','-1','-n','-p',str(args.jobs)],stdin=subprocess.PIPE,stdout=dst,stderr=subprocess.PIPE)
        while body:=src.read(8*M):
            process.stdin.write(body)
        process.stdin.close();error=process.stderr.read().decode();status=process.wait()
        require(status==0,'pigz failed: '+error)
    raw_sha=None
    report={'operation':'private-full-gpt-disk-image','image':str(image),'image_bytes':image.stat().st_size,
            'image_sha256':raw_sha,'compressed_image':str(compressed),'compressed_bytes':compressed.stat().st_size,
            'compressed_sha256':None,'gzip_full_stream_verified':False,'gpt_primary_backup_verified':not build_only,
            'source_recovery_zip_sha256':None, 'source_recovery_raw_sha256':None,
            'source_private_baseline':selected['baseline'],'single_user_parent_sha256':None,
            'template':str(template) if template else None,'template_layout_preserved':bool(template),
            'kernel_mode':getattr(args,'kernel_mode','prebuilt'),
            'final_initramfs_sha256':None,'storage_payload':audit,
            'pristine_partition_sha256':partition_hashes,'fresh_filesystems':fresh,'efi_files':files,
            'all_efi_files_readback_verified':False,'content_hash_validation':False,'build_only_requested':build_only,'new_rollback_performed':False,'device_written':False,'hardware_boot_tested':False,
            'private_identity_present':True,'anonymous_public_image':False,'userdata_migrated':False,
            'hardware_scope':('New source-compiled kernel candidate with accepted SQ2 userspace inputs; boot/user0/SQ1 untested'
                              if getattr(args,'kernel_mode','prebuilt')=='source' else
                              'SQ2 accepted runtime inputs; new disk boot, user-0 applications and SQ1 remain untested')}
    (output/'IMAGE-MANIFEST.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ['image','image_bytes','image_sha256','compressed_image','compressed_bytes','compressed_sha256','final_initramfs_sha256','hardware_boot_tested']},indent=2),flush=True)
    return report
