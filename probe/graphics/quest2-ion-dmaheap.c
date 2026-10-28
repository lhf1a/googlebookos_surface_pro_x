/* Private Quest2 probe adapter. A real DMA-heap fd replaces the unavailable
 * ION control fd. Allocation requests are logged and rejected until their
 * exact heap/cache/security semantics are implemented and verified. */
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>

int ion_open(void) {
    int fd=open("/dev/dma_heap/system",O_RDONLY|O_CLOEXEC);
    fprintf(stderr,"QUEST2_ION_OPEN system_heap_fd=%d\n",fd);
    return fd;
}
int ion_close(int fd) { return close(fd); }
int ion_alloc_fd(int fd,size_t len,size_t align,unsigned int heap_mask,
                 unsigned int flags,int *handle_fd) {
    (void)fd;
    fprintf(stderr,"QUEST2_ION_ALLOC_UNSUPPORTED len=%zu align=%zu mask=0x%x flags=0x%x\n",
            len,align,heap_mask,flags);
    if(handle_fd) *handle_fd=-1;
    errno=ENOTSUP;
    return -ENOTSUP;
}
