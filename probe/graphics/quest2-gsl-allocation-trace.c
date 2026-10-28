/* Private diagnostic interposition only: forward all GSL arguments/results.
 * The matched v77 AArch64 ABI is (uint64_t size, uint64_t flags, void *out).
 * No host pointers or payloads are logged. Not used in the system candidate. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <link.h>
#include <string.h>

typedef int (*Allocate)(uint64_t,uint64_t,void*);
static Allocate original;
static pthread_once_t once=PTHREAD_ONCE_INIT;
static atomic_uint count;
static int loaded(struct dl_phdr_info *info,size_t size,void *unused) {
    (void)size;(void)unused;
    if(strcmp(info->dlpi_name,"/vendor/lib64/egl/libgsl.so"))return 0;
    /* Fixed pinned GSL entry. Do not dlopen it from the preload's system
     * namespace: that would create a second, uninitialized GSL instance. */
    const uintptr_t offset=0xca658;
    const uint32_t expected[]={0xd10183ff,0xa9027bfd,0x910083fd,0xa9035ff8};
    for(unsigned i=0;i<info->dlpi_phnum;i++) {
        const ElfW(Phdr) *p=&info->dlpi_phdr[i];
        if(p->p_type==PT_LOAD && (p->p_flags&PF_X) && offset>=p->p_vaddr &&
           offset+sizeof(expected)<=p->p_vaddr+p->p_memsz &&
           !memcmp((void*)(info->dlpi_addr+offset),expected,sizeof(expected))) {
            if(original){fputs("GSL_TRACE_DUPLICATE_INSTANCE\n",stderr);abort();}
            original=(Allocate)(info->dlpi_addr+offset);
        }
    }
    return 0;
}
static void resolve(void) {
    dl_iterate_phdr(loaded,NULL);
    if(!original){fputs("GSL_TRACE_RESOLVE_FAILED\n",stderr);abort();}
}
int gsl_memory_alloc_pure_64(uint64_t size,uint64_t flags,void *out) {
    pthread_once(&once,resolve);
    unsigned serial=atomic_fetch_add(&count,1);
    int result=original(size,flags,out);
    if(serial<128)fprintf(stderr,"GSL_ALLOC_TRACE serial=%u size=%" PRIu64 " flags=%" PRIx64 " result=%d\n",
        serial,size,flags,result);
    return result;
}
