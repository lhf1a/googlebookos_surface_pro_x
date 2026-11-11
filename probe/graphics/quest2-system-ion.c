/* SPDX-License-Identifier: MIT
 * Private libion ABI for the matched QTI gralloc candidate. Never install over
 * the system libion: only the private donor copy names this DSO in DT_NEEDED.
 */
#define _GNU_SOURCE
#include <stdarg.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
static int q2_real_ioctl(int fd,unsigned long request,void *argument) {
    return (int)syscall(SYS_ioctl,fd,request,argument);
}
#include "quest2-system-ion.h"
int ion_open(void) {return open("/dev/dma_heap/system",O_RDONLY|O_CLOEXEC);}
int ion_close(int fd) {return close(fd);}
int ion_alloc_fd(int fd,size_t len,size_t align,unsigned mask,unsigned flags,int *handle) {
    return q2_system_ion_alloc(fd,len,align,mask,flags,handle);
}
