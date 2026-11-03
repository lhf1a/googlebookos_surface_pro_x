#!/usr/bin/env python3
"""PROJECT STANDARD BUILD ENTRY: one offline SQ1/SQ2 universal disk image.

Reuses all accepted mainline runtime drivers as a matched kernel/module set.
Never writes a block device, installs, boots, or runs a functional test.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import sys
import gzip
import os
from concurrent.futures import ThreadPoolExecutor
from collections import deque

R = Path(__file__).resolve().parents[1]
TEMPLATE_SHA = '95777b7bae18d975786286d8fff214c6d95974397c576d4654ce305e26571aa0'
ESP = 14875099136
ESP_SIZE = 2147483648
SIZE = 21474836480


def digest(p):
    with p.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--template', type=Path, default=R/'private/build-inputs/template.img', help='regular 20GiB Android template, NOT a device')
    p.add_argument('--sealed-input', type=Path, default=R/'private/build-inputs/runtime')
    p.add_argument('--gnu-efi', type=Path, default=Path('/root/.cache/sq1-default-el2-20261102/launcher/external/gnu-efi'), help='built ARM64 GNU-EFI source tree')
    p.add_argument('--output', type=Path, required=True, help='fresh directory, preferably Linux filesystem')
    p.add_argument('--profiles', type=Path, default=R/'configs/surface-pro-x-profiles.json',
                   help='override model defaults; not a mandatory capability checklist')
    p.add_argument('--dpi', type=int, default=300, help='default Android UI density; saved user overrides remain untouched')
    p.add_argument('--ksu-manager', choices=('on', 'off'), default='on', help='install pinned KernelSU Next userspace at first boot')
    p.add_argument('--ksu-bundle', type=Path, default=R/'private/build-inputs/ksunext', help='pinned ksud and manager-verified.apk; never replaces kernel')
    p.add_argument('--touch-repair', choices=('on','off'), default='on', help='generic touch repair baseline; off explicitly retains legacy runtime')
    p.add_argument('--touch-bundle', type=Path, default=R/'private/build-inputs/touch', help='merged calibration/noise bundle from build-unified-touch.py')
    p.add_argument('--lte', choices=('on', 'off'), default='on',
                   help='LTE/Radio baseline on by default; off is the explicit original-runtime option')
    p.add_argument('--lte-bundle', type=Path, default=R/'private/build-inputs/lte',
                   help='private raw LTE inputs; Radio compiled from source, unit guard retained')
    p.add_argument('--keep-work', action='store_true', help='retain intermediates for debugging')
    p.add_argument('--suspend-policy', choices=('android','screen-only'), default='android')
    p.add_argument('--power-bundle', type=Path, default=R/'private/build-inputs/power')
    a = p.parse_args()
    if not 72 <= a.dpi <= 1000:
        p.error('--dpi must be between 72 and 1000')
    if not stat.S_ISREG(a.template.stat().st_mode) or a.template.stat().st_size != SIZE:
        p.error('Template must be the pinned regular image, not a block device')
    if a.output.exists():
        p.error('Output must not exist')
    a.output.mkdir(parents=True)
    log = (a.output/'VERIFICATION.txt').open('w')

    def run(args):
        args = list(map(str, args))
        r = subprocess.run(args, capture_output=True)
        stdout = ('binary bytes='+str(len(r.stdout))+' SHA256='+hashlib.sha256(r.stdout).hexdigest()
                  if args[0]=='mtype' else r.stdout.decode(errors='replace'))
        log.write('COMMAND '+json.dumps(args)+'\nSTDOUT\n'+stdout+
                  '\nSTDERR\n'+r.stderr.decode(errors='replace')+'\nEXIT '+str(r.returncode)+'\n')
        log.flush()
        if r.returncode:
            raise RuntimeError('Command failed; see '+str(a.output/'VERIFICATION.txt'))
        return r.stdout

    command=[sys.executable, R/'scripts/build-surface-pro-x.py', '--machine', 'both',
             '--sealed-input', a.sealed_input, '--profiles', a.profiles, '--output', a.output/'profiles',
             '--lte', a.lte, '--lte-bundle', a.lte_bundle]
    command+=['--touch-repair',a.touch_repair,'--touch-bundle',a.touch_bundle]
    command+=['--dpi',str(a.dpi),'--ksu-manager',a.ksu_manager,'--ksu-bundle',a.ksu_bundle,'--suspend-policy',a.suspend_policy,'--power-bundle',a.power_bundle]
    run(command)
    # LTE source compiler is invoked by profile builder; private files are inputs, not Radio04.
    common = {entry: a.output/'profiles/SQ1/EFI'/('SQ1'+entry)/'initramfs.cpio.gz'
              for entry in ('Default', 'Fallback')}
    common_hash = digest(common['Default'])
    # A single fresh-image storage/identity profile for both machines. Runtime
    # drivers/ABI are the same accepted 957b baseline; model DT retains 661/700.
    root = a.output/'esp-root'
    (root/'EFI/BOOT').mkdir(parents=True)
    (root/'EFI/SPX').mkdir()
    source_fat = str(a.template)+'@@'+str(ESP)
    run(['mcopy', '-i', source_fat, '::/EFI/BOOT/BOOTAA64.EFI', root/'EFI/BOOT/BOOTAA64.EFI'])
    run(['mcopy', '-i', source_fat, '::/SQ2-SSD-ID.txt', root/'SQ2-SSD-ID.txt'])
    for model in ('SQ1', 'SQ2'):
        for entry in ('Default', 'Fallback'):
            src = a.output/'profiles'/model/'EFI'/(model+entry)
            dst = root/'EFI'/(model+entry)
            shutil.copytree(src, dst)
            shutil.copyfile(common[entry], dst/'initramfs.cpio.gz')
            rows = {f.name: digest(f) for f in dst.iterdir() if f.name != 'SHA256SUMS'}
            (dst/'SHA256SUMS').write_text(''.join(h+'  '+n+'\n' for n,h in sorted(rows.items())))
    shutil.copytree(a.output/'profiles/SQ1/firmware', root/'firmware')
    g = a.gnu_efi
    for fallback, name in ((0, 'default'), (1, 'fallback')):
        obj=a.output/(name+'.o'); elf=a.output/(name+'.so')
        run(['aarch64-linux-gnu-gcc', '-Wall', '-Wextra', '-Werror', '-fpic', '-fshort-wchar',
             '-fno-stack-protector', '-ffreestanding', '-mstrict-align', '-DCONFIG_aarch64',
             '-D__MAKEWITH_GNUEFI', '-DGNU_EFI_USE_MS_ABI', '-DFALLBACK='+str(fallback),
             '-I'+str(g/'inc'), '-I'+str(g/'inc/aarch64'), '-I'+str(g/'inc/protocol'),
             '-c', R/'probe/virtualization/spx-model-select.c', '-o', obj])
        run(['aarch64-linux-gnu-gcc', '-nostdlib', '-shared', '-Wl,-Bsymbolic',
             '-Wl,--no-wchar-size-warning', '-Wl,--defsym=EFI_SUBSYSTEM=10',
             '-T', g/'gnuefi/elf_aarch64_efi.lds', g/'aarch64/gnuefi/crt0-efi-aarch64.o', obj,
             '-L'+str(g/'aarch64/lib'), '-L'+str(g/'aarch64/gnuefi'), '-lefi', '-lgnuefi', '-o', elf])
        run(['aarch64-linux-gnu-objcopy', '-j', '.text', '-j', '.sdata', '-j', '.data',
             '-j', '.dynamic', '-j', '.dynsym', '-j', '.rel*', '-j', '.rela*', '-j', '.reloc',
             '-j', '.eh_frame', '-O', 'binary', elf, root/'EFI/SPX'/(name+'.efi')])
        raw=(root/'EFI/SPX'/(name+'.efi')).read_bytes()
        pe=struct.unpack_from('<I',raw,60)[0]
        if raw[:2]!=b'MZ' or raw[pe:pe+4]!=b'PE\0\0': raise ValueError('Invalid EFI artifact')
    menu='insmod part_gpt\ninsmod fat\ninsmod chain\nset timeout=10\nset default=spx-default\n'
    for name, title in [('default','Qualcomm Vulkan'),('fallback','Mesa fallback')]:
        menu += (f"menuentry 'Surface Pro X SQ1/SQ2 - {title}' --id spx-{name} {{\n"
                 " search --no-floppy --fs-uuid --set=root B026-1027\n"
                 f" chainloader /EFI/SPX/{name}.efi\n}}\n")
    (root/'EFI/BOOT/grub.cfg').write_text(menu)
    esp=a.output/'esp.fat'
    with esp.open('xb') as f: f.truncate(ESP_SIZE)
    run(['mkfs.fat','-F','32','-i','B0261027','-n','SPXBOOT',esp])
    run(['mcopy','-i',esp,'-s',root/'EFI','::/'])
    run(['mcopy','-i',esp,'-s',root/'firmware','::/'])
    run(['mcopy','-i',esp,root/'SQ2-SSD-ID.txt','::/'])
    image=a.output/'surface-pro-x-sq1-sq2.img'
    run(['cp','--sparse=always','--reflink=auto',a.template,image])
    with image.open('r+b') as dst, esp.open('rb') as src:
        dst.seek(ESP); shutil.copyfileobj(src,dst,4*1024*1024)
    fat=str(image)+'@@'+str(ESP)
    hashes={}
    for f in sorted(root.rglob('*')):
        if not f.is_file(): continue
        rel=f.relative_to(root).as_posix()
        body=run(['mtype','-i',fat,'::/'+rel])
        if hashlib.sha256(body).hexdigest()!=digest(f): raise ValueError('Readback mismatch: '+rel)
        hashes[rel]=digest(f)
    # Outside the ESP every byte is inherited from the pinned template.
    for off,length in ((0,ESP),(ESP+ESP_SIZE,SIZE-ESP-ESP_SIZE)):
        with image.open('rb') as x,a.template.open('rb') as y:
            x.seek(off);y.seek(off)
            while length:
                n=min(length,4*1024*1024)
                if x.read(n)!=y.read(n): raise ValueError('Non-ESP partition changed')
                length-=n
    report=dict(standard_entry='scripts/build.py',format='single-raw-disk-image',
                image=str(image.resolve()),sha256=digest(image),bytes=SIZE,
                template_sha256='not gated; operator supplied template',common_initramfs_sha256=common_hash,
                fallback_initramfs_sha256=digest(common['Fallback']),lte=a.lte,touch_repair=a.touch_repair,
                dpi=a.dpi,ksu_manager=a.ksu_manager,
                model_selection='bounded SMBIOS type4 Microsoft SQ1/SQ2; unknown refuses',
                entries=2,hardware_tested=False,deployed=False,files=hashes,
                baseline='fee67b2 accepted runtime + 0100429 LTE/Radio when enabled; original Mesa fallback retained')
    (a.output/'build-audit.json').write_text(json.dumps(report,indent=2)+'\n')
    (a.output/'SHA256SUMS').write_text(report['sha256']+'  '+image.name+'\n')
    compressed=image.with_suffix('.img.gz')
    # Bounded parallel, concatenated standard gzip members; memory < 256MiB.
    with image.open('rb') as src, compressed.open('xb') as dst, ThreadPoolExecutor(max_workers=8) as pool:
        queue=deque()
        while block:=src.read(8*1024*1024):
            queue.append(pool.submit(gzip.compress,block,compresslevel=1,mtime=0))
            if len(queue)>=16:dst.write(queue.popleft().result())
        while queue:dst.write(queue.popleft().result())
    with gzip.open(compressed,'rb') as check:
        h=hashlib.sha256()
        while block:=check.read(4*1024*1024):h.update(block)
    if h.hexdigest()!=report['sha256']:raise ValueError('Compressed readback differs')
    report['compressed_image']=str(compressed.resolve())
    report['compressed_bytes']=compressed.stat().st_size
    report['compressed_sha256']=digest(compressed)
    (a.output/'build-audit.json').write_text(json.dumps(report,indent=2)+'\n')
    (a.output/'SHA256SUMS').write_text(report['compressed_sha256']+'  '+compressed.name+'\n')
    log.close()
    if not a.keep_work:
        for name in ('profiles','esp-root','esp.fat','default.o','default.so','fallback.o','fallback.so',image.name):
            item=a.output/name
            if item.resolve().parent!=a.output.resolve():raise ValueError('Cleanup escaped output')
            if item.is_dir():shutil.rmtree(item)
            elif item.exists():item.unlink()
    print(json.dumps(report,indent=2))


if __name__=='__main__': main()
