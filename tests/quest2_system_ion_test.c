/* Execute the actual adapter with heap syscalls mocked; retain real fd lifetime. */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/dma-heap.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
static unsigned calls, opens, closed;
static int fault, control, heap = -1;
static long test_syscall(long request, ...) {
    va_list ap; va_start(ap,request); long result;
    if(request==SYS_openat) {
        assert(va_arg(ap,int)==AT_FDCWD);
        assert(!strcmp(va_arg(ap,const char *),"/dev/dma_heap/system-uncached"));
        assert(va_arg(ap,int)==(O_RDONLY|O_CLOEXEC)); opens++;
        if(fault==2) {errno=ENOENT;result=-1;}
        else result=heap=open("/dev/null",O_RDONLY|O_CLOEXEC);
    } else {
        assert(request==SYS_close);
        assert(va_arg(ap,int)==heap); closed++;
        result=close(heap);
        errno=EBUSY; /* cleanup must not replace the allocation failure */
    }
    va_end(ap); return result;
}
static int q2_real_ioctl(int fd,unsigned long request,void *arg) {
    calls++; assert(request==DMA_HEAP_IOCTL_ALLOC);
    assert(fd==control || fd==heap);
    assert(fcntl(fd,F_GETFD)>=0);
    struct dma_heap_allocation_data *a=arg;
    assert(a->len==16384 && a->fd_flags==(O_RDWR|O_CLOEXEC) && !a->heap_flags);
    if(fault==1) {errno=ENOMEM;return -1;}
    a->fd=open("/dev/null",O_RDWR|O_CLOEXEC); return 0;
}
#define syscall test_syscall
#include "../probe/graphics/quest2-system-ion.h"
#undef syscall
int main(void) {
    control=open("/dev/null",O_RDONLY|O_CLOEXEC); assert(control>=0);
    int result=99;
    assert(q2_system_ion_alloc(control,1,0,1U<<25,1,NULL)==-EINVAL);
    assert(q2_system_ion_alloc(control,0,0,1U<<25,1,&result)==-EINVAL && result==-1);
    const unsigned bad_masks[]={0,1,1U<<31,(1U<<25)|1};
    for(unsigned i=0;i<4;i++)
        assert(q2_system_ion_alloc(control,16384,0,bad_masks[i],1,&result)==-EOPNOTSUPP && result==-1);
    const size_t bad_align[]={3,2*(size_t)sysconf(_SC_PAGESIZE),SIZE_MAX};
    for(unsigned i=0;i<3;i++)
        assert(q2_system_ion_alloc(control,16384,bad_align[i],1U<<25,1,&result)==-EOPNOTSUPP);
    assert(q2_system_ion_alloc(control,16384,0,1U<<25,2,&result)==-EOPNOTSUPP);
    assert(!calls && !opens);
    for(unsigned cached=0;cached<=1;cached++) {
        const size_t good_align[]={0,1,64,(size_t)sysconf(_SC_PAGESIZE)};
        for(unsigned i=0;i<4;i++) {
            assert(!q2_system_ion_alloc(control,16384,good_align[i],1U<<25,cached,&result));
            assert(result>=0 && (fcntl(result,F_GETFD)&FD_CLOEXEC)); assert(!close(result));
            assert(fcntl(control,F_GETFD)>=0);
        }
        fault=1;
        assert(q2_system_ion_alloc(control,16384,0,1U<<25,cached,&result)==-ENOMEM && result==-1 && errno==ENOMEM);
        fault=0;
        assert(q2_system_ion_alloc(-1,16384,0,1U<<25,cached,&result)==-EBADF && result==-1);
    }
    assert(opens==5 && closed==5);
    unsigned before=calls; fault=2;
    assert(q2_system_ion_alloc(control,16384,0,1U<<25,0,&result)==-ENOENT && result==-1);
    assert(calls==before && closed==5); /* no cached/DRM fallback */
    assert(!close(control)); puts("SYSTEM_ION_TEST_PASS");
}
