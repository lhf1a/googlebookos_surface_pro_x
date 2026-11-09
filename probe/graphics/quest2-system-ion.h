/* SPDX-License-Identifier: MIT
 * Shared nonsecure system-heap backend for the VIDC and private gralloc probes.
 * Caller supplies q2_real_ioctl and q2_trace. No driver/service installation.
 * shortcut: uncached allocations use MSM GEM WC; replace this backend before
 * removing the MSM GPU driver for KGSL. Cached allocations use DMA-heap/system.
 */
#ifndef QUEST2_SYSTEM_ION_H
#define QUEST2_SYSTEM_ION_H
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/dma-heap.h>
#include <drm/msm_drm.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
/* Linux MSM uses pgprot_writecombine for WC GEM mmap and cleans initial
 * pages before sharing. PRIME preserves that mapping and the DMA-buf lifetime.
 * No GPU command is submitted; protected or special ION heaps remain unsupported. */
static int q2_wc_allocate_on_fd(int drm,size_t len,int *handle) {
    struct drm_msm_gem_new create={.size=len,.flags=MSM_BO_WC};
    if (q2_real_ioctl(drm,DRM_IOCTL_MSM_GEM_NEW,&create)) return -1;
    struct drm_msm_gem_info info={.handle=create.handle,.info=0x05}; /* MSM_INFO_GET_FLAGS */
    struct drm_prime_handle prime={.handle=create.handle,.flags=DRM_CLOEXEC|DRM_RDWR,.fd=-1};
    int r=q2_real_ioctl(drm,DRM_IOCTL_MSM_GEM_INFO,&info),error=errno;
    if (!r && info.value!=MSM_BO_WC) {r=-1;error=EPROTO;}
    if (!r) {r=q2_real_ioctl(drm,DRM_IOCTL_PRIME_HANDLE_TO_FD,&prime);error=errno;}
    struct drm_gem_close release={.handle=create.handle};
    if (q2_real_ioctl(drm,DRM_IOCTL_GEM_CLOSE,&release) && !r) {r=-1;error=errno;}
    if (r) {
        if (prime.fd>=0) (void)syscall(SYS_close,prime.fd);
        errno=error;return -1;
    }
    *handle=prime.fd;return 0;
}
static int q2_wc_allocate(size_t len,int *handle) {
    DIR *dir=opendir("/dev/dri");if (!dir) return -1;
    struct dirent *entry;int result=-1,error=ENODEV;
    while ((entry=readdir(dir))) {
        if (strncmp(entry->d_name,"renderD",7)) continue;
        const char *n=entry->d_name+7;
        if (!*n || strspn(n,"0123456789")!=strlen(n)) continue;
        char path[PATH_MAX];snprintf(path,sizeof(path),"/dev/dri/%s",entry->d_name);
        int drm=(int)syscall(SYS_openat,AT_FDCWD,path,O_RDWR|O_CLOEXEC,0);
        if (drm<0) {error=errno;continue;}
        char name[64]={0};struct drm_version version={.name_len=sizeof(name)-1,.name=name};
        struct stat st;
        int match=!fstat(drm,&st) && S_ISCHR(st.st_mode) &&
            !q2_real_ioctl(drm,DRM_IOCTL_VERSION,&version) && !strcmp(name,"msm");
        if (match) {result=q2_wc_allocate_on_fd(drm,len,handle);error=result?errno:0;}
        (void)syscall(SYS_close,drm);
        if (match) break;
    }
    closedir(dir);
    errno=error;return result;
}
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
    if (!flags) {
        if (fcntl(fd,F_GETFD)<0) return -errno;
        if (q2_wc_allocate(len,handle)) return -errno;
        return 0;
    }
    struct dma_heap_allocation_data allocation={.len=len,.fd_flags=O_RDWR|O_CLOEXEC};
    int r=q2_real_ioctl(fd,DMA_HEAP_IOCTL_ALLOC,&allocation),error=r?errno:0;
    if (r) {errno=error;return -error;}
    *handle=(int)allocation.fd;return 0;
}
#endif
