#!/usr/bin/env python3
"""Build the default model router plus read-only own-FSG collection; no tests."""
import argparse,json,pathlib,subprocess
R=pathlib.Path(__file__).resolve().parents[1]
def build(output,gnu_efi):
    output, g=pathlib.Path(output),pathlib.Path(gnu_efi)
    output.mkdir(parents=True,exist_ok=True)
    source=R/'probe/lte/fsg-efi'; records=[]
    def run(cmd):
        cmd=list(map(str,cmd));p=subprocess.run(cmd,capture_output=True,text=True)
        records.append(dict(command=cmd,stdout=p.stdout,stderr=p.stderr,exit=p.returncode))
        (output/'commands.json').write_text(json.dumps(records,indent=2)+'\n')
        if p.returncode:raise RuntimeError(p.stderr)
    objects=[]
    for name,path in [('router',R/'probe/virtualization/spx-model-select.c'),('auto',source/'auto.c'),('transport',source/'transport.c'),('cache',source/'external/cache.c'),('compat',source/'compat.c')]:
        obj=output/(name+'.o');objects.append(obj)
        run(['aarch64-linux-gnu-gcc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-O2','-fshort-wchar','-ffreestanding','-U_FORTIFY_SOURCE','-D_FORTIFY_SOURCE=0','-fno-stack-protector','-fpic','-mstrict-align','-DCONFIG_aarch64','-D__MAKEWITH_GNUEFI','-DGNU_EFI_USE_MS_ABI','-DSPX_LTE_AUTO=1',*(['-DSTATIC=static'] if name=='cache' else []),'-I'+str(source),'-I'+str(g/'inc'),'-I'+str(g/'inc/aarch64'),'-c',path,'-o',obj])
    elf=output/'default.so';efi=output/'default.efi'
    run(['aarch64-linux-gnu-gcc','-nostdlib','-shared','-Wl,-Bsymbolic','-Wl,--no-undefined','-Wl,--no-wchar-size-warning','-Wl,--defsym=EFI_SUBSYSTEM=10','-T',g/'gnuefi/elf_aarch64_efi.lds',g/'aarch64/gnuefi/crt0-efi-aarch64.o',*objects,'-L'+str(g/'aarch64/lib'),'-L'+str(g/'aarch64/gnuefi'),'-lefi','-lgnuefi','-o',elf])
    run(['aarch64-linux-gnu-objcopy','-j','.text','-j','.sdata','-j','.data','-j','.dynamic','-j','.dynsym','-j','.rel*','-j','.rela*','-j','.reloc','-j','.eh_frame','-O','binary',elf,efi])
    return efi
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--output',required=True,type=pathlib.Path);p.add_argument('--gnu-efi',required=True,type=pathlib.Path);a=p.parse_args();print(build(a.output,a.gnu_efi))
