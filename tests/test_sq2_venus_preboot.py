"""Compile the actual EFI handoff and Linux dispatch with host hardware stubs."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
LIB = Path('/root/.cache/sq2-preboot-v15/external/dtc/libfdt')


def run(directory, name, code, fdt=False):
    source = directory / (name + '.c')
    source.write_text(code)
    command = ['gcc', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
               '-Wno-sign-compare', '-fsanitize=address,undefined', '-g',
               '-fno-pie', '-no-pie', str(source), '-o', str(directory / name)]
    if fdt:
        command += ['-I' + str(LIB)] + [str(LIB / n) for n in (
            'fdt.c', 'fdt_ro.c', 'fdt_wip.c', 'fdt_rw.c', 'fdt_sw.c',
            'fdt_strerror.c', 'fdt_empty_tree.c', 'fdt_addresses.c', 'fdt_check.c')]
    subprocess.run(command, check=True)
    subprocess.run([str(directory / name)], check=True)


efi = (ROOT / 'probe/virtualization/sq2-venus-preboot.c').read_text()
efi = '\n'.join(line for line in efi.splitlines() if not line.startswith('#include'))
efi_prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <libfdt.h>
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef int BOOLEAN;
typedef int EFI_STATUS;
#define TRUE 1
#define FALSE 0
#define EFI_SUCCESS 0
#define EFI_ACCESS_DENIED 1
#define EFI_INVALID_PARAMETER 2
#define EFI_NOT_FOUND 3
#define EFI_SECURITY_VIOLATION 4
#define EFI_DEVICE_ERROR 5
#define EFI_NOT_READY 6
#define EFI_ERROR(s) ((s)!=0)
#define EFI_PAGE_SIZE 4096
#define CompareMem memcmp
#define ZeroMem(p,n) memset(p,0,n)
#define Print(...) ((void)0)
static const unsigned char sq2_venus_metadata[16384] = {7,8,9};
struct pil_fw {void *elf_data; size_t elf_size; uintptr_t metadata;};
struct image {int DeviceHandle;};
static struct image loaded_image;
static struct image *LoadedImage = &loaded_image;
struct file;
typedef struct file *EFI_FILE_HANDLE;
struct file {void (*Close)(struct file *);};
static void close_file(struct file *f) {}
static struct file file = {close_file};
static EFI_FILE_HANDLE LibOpenRoot(int handle) {return &file;}
static int bad_image, bad_metadata, init_fail, mem_fail, init_calls, mem_calls, flushes;
static UINT64 sq2_pas_result[4];
static void fw_free(struct pil_fw *f) {free((void *)f->metadata); f->metadata=0;}
static EFI_STATUS fw_prepare(struct pil_fw *f, EFI_FILE_HANDLE root,const char *name) {
 assert(!strcmp(name,"qcom/venus-5.2/venus.mbn"));
 f->elf_size=1159200; f->metadata=(uintptr_t)calloc(1,16384); assert(f->metadata); return 0;
}
static EFI_STATUS fw_check(struct pil_fw *f) {return bad_image;}
static EFI_STATUS fw_load_metadata(struct pil_fw *f) {
 memcpy((void *)f->metadata,sq2_venus_metadata,16384);
 if(bad_metadata) ((char *)f->metadata)[10]^=1;
 return 0;
}
static void WriteBackInvalidateDataCacheRange(void *p,size_t n) {assert(p&&n);flushes++;}
static EFI_STATUS scm_pil_init(int id,uintptr_t p) {
 assert(id==9&&p);init_calls++;sq2_pas_result[0]=1;sq2_pas_result[1]=init_fail;return 0;
}
static EFI_STATUS scm_pil_mem_setup(int id,uintptr_t p,size_t n) {
 assert(id==9&&p==0x8c000000&&n==0x500000);mem_calls++;
 sq2_pas_result[0]=1;sq2_pas_result[1]=mem_fail;return 0;
}
'''
efi_test = r'''
static char src[4096] __attribute__((aligned(8))), dst[4096] __attribute__((aligned(8)));
static void reset(void) {
 fw_free(&venus);memset(&venus,0,sizeof(venus));venus_dtb=NULL;
 venus_touched=venus_ready=FALSE;bad_image=bad_metadata=init_fail=mem_fail=0;
 init_calls=mem_calls=flushes=0;
 assert(!fdt_create_empty_tree(src,sizeof(src)));
 const char compatible[]="microsoft,surface-pro-x\0qcom,sc8180x";
 assert(!fdt_setprop(src,0,"compatible",compatible,sizeof(compatible)));
 fdt32_t p[]={0,cpu_to_fdt32(9),cpu_to_fdt32(0x8c000000),cpu_to_fdt32(0x500000)};
 assert(!fdt_setprop(src,0,"microsoft,el2-venus-prepared",p,sizeof(p)));
 memcpy(dst,src,sizeof(src));
}
int main(void) {
 reset();assert(!sq2_venus_prepare(src));assert(!sq2_venus_finish());
 assert(init_calls==1&&mem_calls==1&&venus_ready&&flushes==1);
 assert(!sq2_venus_finish());assert(init_calls==1&&mem_calls==1);
 assert(!sq2_venus_publish(dst));assert(venus_check_contract(dst,1));
 assert(!sq2_venus_publish(dst));assert(sq2_venus_prepare(src)==EFI_ACCESS_DENIED);
 reset();assert(sq2_venus_finish()==EFI_NOT_READY);assert(sq2_venus_publish(dst)==EFI_NOT_READY);
 reset();bad_metadata=1;assert(sq2_venus_prepare(src)==EFI_SECURITY_VIOLATION);assert(!init_calls);
 reset();bad_image=EFI_SECURITY_VIOLATION;assert(sq2_venus_prepare(src)==EFI_SECURITY_VIOLATION);
 for(int cell=0;cell<4;cell++) {
  reset();int len;fdt32_t *p=fdt_getprop_w(src,0,"microsoft,el2-venus-prepared",&len);
  p[cell]=cpu_to_fdt32(fdt32_to_cpu(p[cell])+1);
  assert(sq2_venus_prepare(src)==EFI_INVALID_PARAMETER);assert(!init_calls&&!mem_calls);
 }
 reset();assert(!fdt_setprop_u32(src,0,"microsoft,el2-venus-prepared",0));
 assert(sq2_venus_prepare(src)==EFI_INVALID_PARAMETER);
 reset();assert(!fdt_setprop_string(src,0,"compatible","qcom,sc8280xp"));
 assert(sq2_venus_prepare(src)==EFI_INVALID_PARAMETER);
 reset();assert(!sq2_venus_prepare(src));init_fail=1;assert(sq2_venus_finish()==EFI_DEVICE_ERROR);
 assert(init_calls==1&&!mem_calls&&!venus_ready&&venus_check_contract(src,0));
 assert(sq2_venus_finish()==EFI_NOT_READY);assert(init_calls==1);
 reset();assert(!sq2_venus_prepare(src));mem_fail=1;assert(sq2_venus_finish()==EFI_DEVICE_ERROR);
 assert(init_calls==1&&mem_calls==1&&!venus_ready&&venus_check_contract(src,0));
 assert(sq2_venus_publish(dst)==EFI_NOT_READY);assert(venus_check_contract(dst,0));
 reset();assert(!sq2_venus_prepare(src));assert(!sq2_venus_finish());
 assert(!fdt_nop_property(dst,0,"microsoft,el2-venus-prepared"));assert(sq2_venus_publish(dst)==EFI_NOT_READY);
 assert(!venus_scm_ok(EFI_DEVICE_ERROR));sq2_pas_result[0]=0;assert(!venus_scm_ok(0));
 fw_free(&venus);puts("PASS EFI: exact contract, metadata, failure, no retry, late publication");
}
'''
linux = (ROOT / 'probe/video/sq2-venus-fw.c').read_text()
linux = linux[linux.index('static int sq2_fw_load('):linux.index('static int sq2_fw_auth(')]
linux_prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#include <arpa/inet.h>
typedef uint32_t __be32;
struct device {int dev;};
struct sq2_venus_fw {struct device *pdev;void *image,*cpu,*no_tz_base;uintptr_t phys;int pil_gap;};
static int sq2_fw_no_tz_load(struct sq2_venus_fw *f) {assert(0);return -EINVAL;}
static int of_root,has_marker=1,length=16,platform=1,span=0x500000;
static __be32 marker[4];
static const char image_name[]="image";
#define SQ2_FW_LIMIT (6*1024*1024)
#define pr_info(...) ((void)0)
#define be32_to_cpup(p) ntohl(*(p))
static const void *of_get_property(int r,const char *n,int *len) {*len=length;return has_marker?marker:NULL;}
static int of_machine_is_compatible(const char *s) {return platform;}
static int qcom_mdt_get_size(void *f) {return span;}
static int normal,no_init,barriers,load_result;
static int qcom_mdt_load(int *d,void *i,const char *n,int pas,void *cpu,uintptr_t phys,size_t size,void *reloc) {normal++;return load_result;}
static int qcom_mdt_load_no_init(int *d,void *i,const char *n,int pas,void *cpu,uintptr_t phys,size_t size,void *reloc) {no_init++;return load_result;}
static void wmb(void) {barriers++;}
'''
linux_test = r'''
static struct device device;
static struct sq2_venus_fw f;
static void reset(void) {
 f=(struct sq2_venus_fw){.pdev=&device,.phys=0x8c000000,.pil_gap=1};
 marker[0]=htonl(1);marker[1]=htonl(9);marker[2]=htonl(0x8c000000);marker[3]=htonl(0x500000);
 has_marker=1;length=16;platform=1;span=0x500000;normal=no_init=barriers=load_result=0;
}
int main(void) {
 reset();assert(!sq2_fw_load(&f));assert(no_init==1&&!normal&&barriers==1);
 reset();has_marker=0;assert(!sq2_fw_load(&f));assert(normal==1&&!no_init&&barriers==1);
 for(int i=0;i<4;i++){reset();marker[i]^=htonl(1);assert(sq2_fw_load(&f)==-EINVAL);assert(!normal&&!no_init&&!barriers);}
 for(int n=0;n<32;n++)if(n!=16){reset();length=n;assert(sq2_fw_load(&f)==-EINVAL);}
 reset();platform=0;assert(sq2_fw_load(&f)==-EINVAL);
 reset();f.pil_gap=0;assert(sq2_fw_load(&f)==-EINVAL);
 reset();f.phys++;assert(sq2_fw_load(&f)==-EINVAL);
 reset();span++;assert(sq2_fw_load(&f)==-EINVAL);
 reset();load_result=-EIO;assert(sq2_fw_load(&f)==-EIO);assert(no_init==1&&!normal&&!barriers);
 puts("PASS Linux: normal dispatch preserved, prepared contract strict, load errors preserved");
}
'''
with tempfile.TemporaryDirectory(prefix='sq2-venus-preboot-') as temp:
    directory = Path(temp)
    run(directory, 'efi', efi_prefix + efi + efi_test, True)
    run(directory, 'linux', linux_prefix + linux + linux_test)
