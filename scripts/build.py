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
    p.add_argument('--template', type=Path, required=True, help='hash-pinned regular 20GiB Android template, NOT a device')
    p.add_argument('--sealed-input', type=Path, default=R/'private/unified-input')
    p.add_argument('--gnu-efi', type=Path, required=True, help='built ARM64 GNU-EFI source tree')
    p.add_argument('--output', type=Path, required=True, help='fresh directory, preferably Linux filesystem')
    p.add_argument('--profiles', type=Path, default=R/'configs/surface-pro-x-profiles.json',
                   help='override model defaults; not a mandatory capability checklist')
    p.add_argument('--touch-bundle', type=Path, help='merged calibration/noise bundle from build-unified-touch.py')
    a = p.parse_args()
    if not stat.S_ISREG(a.template.stat().st_mode) or a.template.stat().st_size != SIZE:
        p.error('Template must be the pinned regular image, not a block device')
    if digest(a.template) != TEMPLATE_SHA:
        p.error('Template SHA mismatch')
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
             '--sealed-input', a.sealed_input, '--profiles', a.profiles, '--output', a.output/'profiles']
    if a.touch_bundle: command+=['--touch-bundle',a.touch_bundle]
    run(command)
    common = a.output/'profiles/SQ1/EFI/SQ1Default/initramfs.cpio.gz'
    common_hash = digest(common)
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
            shutil.copyfile(common, dst/'initramfs.cpio.gz')
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
                template_sha256=TEMPLATE_SHA,common_initramfs_sha256=common_hash,
                model_selection='bounded SMBIOS type4 Microsoft SQ1/SQ2; unknown refuses',
                entries=2,hardware_tested=False,deployed=False,files=hashes,
                baseline='origin/main fee67b2 accepted runtime; no rejected experiments or mixed module ABI')
    (a.output/'build-audit.json').write_text(json.dumps(report,indent=2)+'\n')
    (a.output/'SHA256SUMS').write_text(report['sha256']+'  '+image.name+'\n')
    print(json.dumps({k:report[k] for k in ('standard_entry','image','sha256','common_initramfs_sha256','hardware_tested')},indent=2))


if __name__=='__main__': main()
