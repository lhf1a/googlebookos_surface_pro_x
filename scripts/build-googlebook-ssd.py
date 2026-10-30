#!/usr/bin/env python3
"""Build a regular sparse whole-disk image; never open a physical device."""
from pathlib import Path
import argparse, hashlib, importlib.util, json, os, re, shutil, struct, subprocess, uuid, zlib
import pwd

R = Path(__file__).resolve().parents[1]
O = R / 'out/ssd'
C = Path('/root/.cache/sq2-probe-build/ssd')
RAW = R / 'downloads/mica-recovery.raw'
SIZE = 128035676160
M = 1024**2
ID = 'SQ2-SSD-20261027-3001237923792379'
DATA_UUID = '71ff412e-9b60-41ab-8975-6266bb795851'
ESP_UUID = 'B026-1027'
BOOT = R / 'out/android-test'
GRUB = R / 'configs/grub-ssd.cfg'
BOOT_FILES = ('Image','sc8180x-surface-pro-x.dtb','native-combined-initramfs.cpio.gz')
IMAGE_NAME = 'googlebook-sq2-ssd.img'
spec = importlib.util.spec_from_file_location('gpt', R/'scripts/inspect-recovery.py')
gpt = importlib.util.module_from_spec(spec); spec.loader.exec_module(gpt)

def run(*cmd): subprocess.run([str(x) for x in cmd], check=True)
def sha(p):
    with Path(p).open('rb') as f: return hashlib.file_digest(f, 'sha256').hexdigest()
def windows_export(image):
    account=pwd.getpwnam('kanoyami')
    directory=Path(account.pw_dir)/'.local/share/sq2-ssd-deploy'
    directory.mkdir(parents=True,exist_ok=True)
    os.chown(directory,account.pw_uid,account.pw_gid); directory.chmod(0o700)
    target=directory/image.name
    if target.exists(): assert os.path.samefile(target,image)
    else: os.link(image,target)
    os.chown(image,account.pw_uid,account.pw_gid); image.chmod(0o600)
    return '\\\\wsl.localhost\\Ubuntu-24.04'+str(target).replace('/','\\')
def region(f, off, length):
    f.seek(off); h = hashlib.sha256()
    while length:
        b = f.read(min(length, 8*M)); assert b
        h.update(b); length -= len(b)
    return h.hexdigest()
def sparse_copy(src, dst, start, length, target):
    src.seek(start); dst.seek(target)
    while length:
        b = src.read(min(length, M)); assert b
        if b.count(0) == len(b): dst.seek(len(b), 1)
        else: dst.write(b)
        length -= len(b)
def allocated(path):
    with path.open('rb') as f:
        off = 0
        while off < path.stat().st_size:
            try: off = os.lseek(f.fileno(), off, os.SEEK_DATA)
            except OSError as e:
                if e.errno == 6: break
                raise
            end = os.lseek(f.fileno(), off, os.SEEK_HOLE)
            while off < end:
                n = min(32*M, end-off)
                yield off, n
                off += n

def write_records(image, parts):
    # A fresh sparse file reads as zero, but an existing SSD does not. Include
    # ALL initialized ext4 metadata/journal blocks even when Linux represents
    # zeroed inode tables as holes. Only free userdata blocks can be skipped.
    data_start=parts['userdata']['offset']
    ranges=[(0,data_start)]
    ranges.extend(allocated(image))
    fs=C/'userdata.ext4'
    header=subprocess.check_output(['dumpe2fs','-h',str(fs)],stderr=subprocess.DEVNULL,text=True)
    inode_blocks=int(re.search(r'Inode blocks per group:\s+(\d+)',header)[1])
    reserved=int(re.search(r'Reserved GDT blocks:\s+(\d+)',header)[1])
    groups=subprocess.check_output(['dumpe2fs','-g',str(fs)],stderr=subprocess.DEVNULL,text=True)
    for line in groups.splitlines():
        if not line or not line[0].isdigit(): continue
        _,_,superblock,gdt,bb,ib,it=line.split(':')
        for block,n in [(int(bb),1),(int(ib),1),(int(it),inode_blocks)]:
            ranges.append((data_start+block*4096,n*4096))
        if superblock!='-1':
            last=int(gdt.split('-')[-1])+reserved
            ranges.append((data_start+int(superblock)*4096,(last-int(superblock)+1)*4096))
    blocks=subprocess.check_output(['debugfs','-R','blocks <8>',str(fs)],stderr=subprocess.DEVNULL,text=True)
    ranges.extend((data_start+int(n)*4096,4096) for n in blocks.split())
    merged=[]
    for off,n in sorted(ranges):
        assert off>=0 and n>0 and off+n<=SIZE
        if merged and off<=merged[-1][1]: merged[-1][1]=max(merged[-1][1],off+n)
        else: merged.append([off,off+n])
    records=[]
    with image.open('rb') as f:
        for start,end in merged:
            for off in range(start,end,32*M):
                n=min(32*M,end-off)
                records.append(dict(offset=off,bytes=n,sha256=region(f,off,n)))
    return records

def prepare():
    O.mkdir(exist_ok=True); C.mkdir(exist_ok=True)
    old = gpt.inspect(RAW)
    parts = {p['name']:p.copy() for p in old['partitions']}
    ota = parts['ota_recovery_b']
    ota['offset'] = parts['userdata']['offset']
    esp_off = ((ota['offset']+ota['bytes']+M-1)//M)*M
    data_off = esp_off+2*1024**3
    parts['userdata'].update(offset=data_off, bytes=((SIZE//512-33)*512-data_off)//4096*4096)
    parts['SQ2PROBE'] = dict(index=6, name='SQ2PROBE', offset=esp_off, bytes=2*1024**3)
    plan = dict(image_bytes=SIZE, identity=ID, data_uuid=DATA_UUID,
                source_build='mica-16471258', partitions=sorted(parts.values(), key=lambda p:p['index']))
    (O/'layout.json').write_text(json.dumps(plan, indent=2)+'\n')
    with RAW.open('rb') as f: header_hash = region(f, parts['super']['offset'], M)
    (O/'sq2-ssd-layout').write_text(f'SSD_SECTORS={SIZE//512}\nSSD_ID={ID}\nSSD_DATA_UUID={DATA_UUID}\nSSD_ESP_UUID={ESP_UUID}\nSSD_SUPER_HEADER_SHA256={header_hash}\n')
    (O/'sq2-ssd-partitions').write_text(''.join(f"{p['name']} {p['index']} {p['bytes']//512}\n" for p in plan['partitions']))
    slices=[]
    for p in json.loads((R/'out/googlebook-stage/logical-partitions.json').read_text()):
        if not p.get('sha256'): continue
        assert len(p['extents']) == 1 and p['extents'][0]['kind'] == 0
        e = p['extents'][0]
        slices.append(f"{p['name']} {e['offset']-parts['super']['offset']} {e['bytes']}\n")
    assert len(slices)==6
    (O/'sq2-ssd-super-slices').write_text(''.join(slices))
    print(json.dumps(plan, indent=2), flush=True)
    return plan, old

def build():
    plan, old = prepare()
    expected = json.loads((R/'configs/sources.lock.json').read_text())['googlebook']['raw_sha256']
    print('Validating original recovery SHA256', flush=True)
    assert sha(RAW) == expected
    image = C/IMAGE_NAME
    assert not image.exists(), 'Refuse to overwrite image; explicitly archive an earlier build'
    parts = {p['name']:p for p in plan['partitions']}
    oldparts = {p['name']:p for p in old['partitions']}
    with RAW.open('rb') as src, image.open('w+b') as dst:
        dst.truncate(SIZE)
        for p in plan['partitions']:
            if p['name'] in ('userdata','SQ2PROBE','misc','metadata'): continue
            print('Copy original partition', p['name'], flush=True)
            sparse_copy(src, dst, oldparts[p['name']]['offset'], p['bytes'], p['offset'])
        src.seek(1024); entries=bytearray(src.read(128*128))
        for p in plan['partitions']:
            pos=(p['index']-1)*128
            if p['name']=='SQ2PROBE':
                entries[pos:pos+128]=bytes(128)
                entries[pos:pos+16]=uuid.UUID('c12a7328-f81f-11d2-ba4b-00a0c93ec93b').bytes_le
                entries[pos+16:pos+32]=uuid.UUID('fe17fe49-9bca-487a-baf8-087c2dba6690').bytes_le
                entries[pos+56:pos+72]='SQ2PROBE'.encode('utf-16le')
            struct.pack_into('<QQ', entries, pos+32, p['offset']//512, (p['offset']+p['bytes'])//512-1)
        total=SIZE//512
        guid=uuid.UUID('3d35f9b8-a603-4c21-a187-eaf47b70f71d').bytes_le
        def header(here, other, table):
            h=bytearray(512)
            struct.pack_into('<8sIIIIQQQQ16sQIII', h, 0, b'EFI PART', 0x10000, 92, 0, 0,
                             here, other, 34, total-34, guid, table, 128, 128, zlib.crc32(entries))
            struct.pack_into('<I', h, 16, zlib.crc32(h[:92]))
            return h
        mbr=bytearray(512); mbr[446:462]=struct.pack('<B3sB3sII',0,b'\0\2\0',0xee,b'\xff'*3,1,min(total-1,0xffffffff)); mbr[510:]=b'\x55\xaa'
        for off,b in [(0,mbr),(512,header(1,total-1,2)),(1024,entries),((total-33)*512,entries),((total-1)*512,header(total-1,1,total-33))]:
            dst.seek(off); dst.write(b)
    for name in ('userdata','metadata'):
        fs=C/(name+'.ext4')
        with fs.open('wb') as f: f.truncate(parts[name]['bytes'])
        options=['-O','extent,64bit,metadata_csum,casefold,encrypt,verity','-E','encoding=utf8,lazy_itable_init=0,lazy_journal_init=0'] if name=='userdata' else ['-E','lazy_itable_init=0,lazy_journal_init=0']
        run('mke2fs','-q','-F','-t','ext4','-b','4096','-m','0','-L','SQ2DATA' if name=='userdata' else 'SQ2META',
            '-U', DATA_UUID if name=='userdata' else 'af674ae3-f405-405f-be5f-7f594a34dc14',*options,fs)
        run('e2fsck','-fn',fs)
        with fs.open('rb') as src, image.open('r+b') as dst:
            for off,n in allocated(fs): sparse_copy(src,dst,off,n,parts[name]['offset']+off)
    tree=C/'efi-tree'; (tree/'EFI/BOOT').mkdir(parents=True,exist_ok=True); (tree/'boot/sq2-android1').mkdir(parents=True,exist_ok=True)
    shutil.copyfile(R/'downloads/bootaa64.efi',tree/'EFI/BOOT/BOOTAA64.EFI')
    shutil.copyfile(GRUB,tree/'EFI/BOOT/grub.cfg')
    (tree/'SQ2-SSD-ID.txt').write_text(ID+'\n')
    (tree/'SQ2-PROBE-MEDIA.txt').write_text('SQ2 external SSD persistent Googlebook development system\n')
    shutil.copyfile(O/'layout.json',tree/'ssd-layout.json')
    for name in BOOT_FILES:
        shutil.copyfile(BOOT/name,tree/'boot/sq2-android1'/name)
    fat=C/'esp.fat'
    with fat.open('wb') as f:f.truncate(parts['SQ2PROBE']['bytes'])
    run('mkfs.vfat','-F32','-n','SQ2PROBE','-i',ESP_UUID.replace('-',''),fat)
    run('mcopy','-i',fat,'-s',*[str(p) for p in tree.iterdir()],'::/')
    run('fsck.fat','-n',fat)
    with fat.open('rb') as src,image.open('r+b') as dst:
        for off,n in allocated(fat): sparse_copy(src,dst,off,n,parts['SQ2PROBE']['offset']+off)
    run('sgdisk','-v',image)
    verified=gpt.inspect(image)
    assert [(p['index'],p['offset'],p['bytes']) for p in verified['partitions']]==[(p['index'],p['offset'],p['bytes']) for p in plan['partitions']]
    with image.open('rb') as f:
        for p in json.loads((R/'out/googlebook-stage/logical-partitions.json').read_text()):
            if p.get('sha256'): assert region(f,p['extents'][0]['offset'],p['bytes'])==p['sha256'],p['name']
    records=write_records(image,parts)
    result=dict(image=str(image),windows_image=windows_export(image),image_bytes=SIZE,identity=ID,extents=records,written_bytes=sum(e['bytes'] for e in records),
                source_sha256=expected,physical_partitions=len(plan['partitions']),gpt_verified=True,logical_a_hashes_verified=True,
                userdata_bytes=parts['userdata']['bytes'],ota_supported=False,
                boot_payload_sha256={n:sha(tree/'boot/sq2-android1'/n) for n in BOOT_FILES})
    (O/'image-manifest.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS: physical GPT, original super, persistent userdata and ARM64 EFI prepared', flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--prepare',action='store_true');p.add_argument('--refresh-manifest',action='store_true');p.add_argument('--update-boot',action='store_true');a=p.parse_args()
    if a.refresh_manifest or a.update_boot:
        manifest=json.loads((O/'image-manifest.json').read_text())
        plan=json.loads((O/'layout.json').read_text())
        if a.update_boot:
            if manifest.get('hardware_video') or manifest.get('display_profiles') or manifest.get('usb_bluetooth'):
                raise ValueError('Use build-source-disk.py to rebuild hardware-video images; legacy update would drop the verified menu')
            esp=next(p for p in plan['partitions'] if p['name']=='SQ2PROBE')
            fat=f"{manifest['image']}@@{esp['offset']}"
            for n in ('Image','sc8180x-surface-pro-x.dtb','native-combined-initramfs.cpio.gz'):
                run('mcopy','-o','-i',fat,R/'out/android-test'/n,'::/boot/sq2-android1/'+n)
                manifest['boot_payload_sha256'][n]=sha(R/'out/android-test'/n)
            run('mcopy','-o','-i',fat,R/'configs/grub-ssd.cfg','::/EFI/BOOT/grub.cfg')
        manifest['extents']=write_records(Path(manifest['image']),{p['name']:p for p in plan['partitions']})
        manifest['written_bytes']=sum(e['bytes'] for e in manifest['extents'])
        manifest['windows_image']=windows_export(Path(manifest['image']))
        (O/'image-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
        print('Initialized metadata + journal write ranges:',manifest['written_bytes'])
    elif a.prepare: prepare()
    else: build()
