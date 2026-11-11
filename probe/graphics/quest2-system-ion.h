/* SPDX-License-Identifier: MIT
 * Shared nonsecure system-heap backend for VIDC and QTI gralloc.
 * Caller supplies q2_real_ioctl.
 * Cached and WC allocations use independent DMA heaps; no GPU device needed.
 * The matching system-uncached exporter is required; no cached/GEM fallback.
 */
#ifndef QUEST2_SYSTEM_ION_H
#define QUEST2_SYSTEM_ION_H
#include <errno.h>
#include <fcntl.h>
#include <linux/dma-heap.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <unistd.h>
/* Exported in both consumers so packers can verify a matched memory backend. */
__attribute__((visibility("default")))
const char quest2_system_ion_backend[] = "dma-heap-system-uncached-v1";

/* The new ION ABI has no alignment field. The donor asks for page alignment;
 * accept only powers of two already guaranteed by these page-backed heaps. */
static int q2_system_ion_alloc(int fd,size_t len,size_t align,unsigned mask,
                               unsigned flags,int *handle) {
    if (!handle) {errno=EINVAL;return -EINVAL;}
    *handle=-1;
    if (!len) {errno=EINVAL;return -EINVAL;}
    long page_size=sysconf(_SC_PAGESIZE);
    if (page_size<=0) {errno=EINVAL;return -EINVAL;}
    if ((align && ((align & (align-1)) || align>(size_t)page_size)) ||
        mask!=(1U<<25) || flags>1) {
        errno=EOPNOTSUPP;return -EOPNOTSUPP;
    }
    if (fcntl(fd,F_GETFD)<0) return -errno;
    int heap=fd;
    if (!flags) {
        heap=(int)syscall(SYS_openat,AT_FDCWD,"/dev/dma_heap/system-uncached",O_RDONLY|O_CLOEXEC,0);
        if (heap<0) return -errno;
    }
    struct dma_heap_allocation_data allocation={.len=len,.fd_flags=O_RDWR|O_CLOEXEC};
    int r=q2_real_ioctl(heap,DMA_HEAP_IOCTL_ALLOC,&allocation),error=r?errno:0;
    if (heap!=fd) (void)syscall(SYS_close,heap);
    if (r) {errno=error;return -error;}
    *handle=(int)allocation.fd;return 0;
}
#endif
