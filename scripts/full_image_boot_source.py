"""Build the small boot-origin EFI application from prepared GNU-EFI source."""
from pathlib import Path
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
GNU_EFI_REVISION = 'aeee48d838d40e8ef787748d5dc066ea2fb99dbe'
CROSS = 'aarch64-linux-gnu-'


def check_inputs(source):
    source = Path(source).resolve()
    if not (source/'inc/efi.h').is_file():
        raise ValueError(f'Prepare GNU-EFI {GNU_EFI_REVISION} at {source} or pass --boot-gnu-efi')
    for tool in ('patchelf', CROSS+'gcc', CROSS+'objcopy', 'make'):
        if not shutil.which(tool):
            raise ValueError(f'Missing portable boot build tool: {tool}')
    return source


def build(output, source, run):
    source = check_inputs(source)
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    g = output/'gnu-efi'
    # Keep dependency inputs pristine; compile in this image's work directory.
    shutil.copytree(source, g, dirs_exist_ok=True,
                    ignore=lambda path, names: set(names) & {'.git', 'aarch64'}
                    if Path(path) == source else set())
    run(['make', '-C', g, 'ARCH=aarch64', 'CROSS_COMPILE='+CROSS,
         '-j' + str(max(1, (os.cpu_count() or 2) - 1))])
    obj, elf, app = (output/p for p in ('Boot.o', 'Boot.so', 'Boot'))
    run([CROSS+'gcc', '-O2','-fpic','-fshort-wchar','-fno-stack-protector',
         '-ffreestanding','-mstrict-align','-DCONFIG_aarch64','-D__MAKEWITH_GNUEFI',
         '-DGNU_EFI_USE_MS_ABI','-I'+str(g/'inc'),'-I'+str(g/'inc/aarch64'),
         '-c',ROOT/'probe/virtualization/spx-boot-source.c','-o',obj])
    run([CROSS+'gcc','-nostdlib','-shared','-Wl,-Bsymbolic','-Wl,--no-undefined',
         '-Wl,--no-wchar-size-warning','-Wl,--defsym=EFI_SUBSYSTEM=10',
         '-T',g/'gnuefi/elf_aarch64_efi.lds',g/'aarch64/gnuefi/crt0-efi-aarch64.o',
         obj,'-L'+str(g/'aarch64/lib'),'-L'+str(g/'aarch64/gnuefi'),'-lefi','-lgnuefi','-o',elf])
    sections = ('.text','.sdata','.data','.rodata','.dynamic','.dynsym','.rel*','.rela*','.reloc','.eh_frame')
    run([CROSS+'objcopy',*[part for sec in sections for part in ('-j',sec)],'-O','efi-app-aarch64',elf,app])
    return app
