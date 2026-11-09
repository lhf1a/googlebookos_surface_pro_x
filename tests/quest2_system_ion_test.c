/* Test real allocation/error-cleanup code with only ioctl mocked. */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/dma-heap.h>
#include <drm/msm_drm.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
static unsigned calls,closed;
static int fault,prime_fd;
static int q2_real_ioctl(int fd,unsigned long request,void *arg) {
    (void)fd;calls++;
    if(request==DMA_HEAP_IOCTL_ALLOC){
        struct dma_heap_allocation_data *a=arg;
        assert(a->len==16384 && a->fd_flags==(O_RDWR|O_CLOEXEC) && !a->heap_flags);
        if(fault){errno=ENOMEM;return -1;}
        a->fd=open("/dev/null",O_RDWR|O_CLOEXEC);return 0;
    }
    if(request==DRM_IOCTL_MSM_GEM_NEW){
        struct drm_msm_gem_new *a=arg;
        assert(a->size==16384 && a->flags==MSM_BO_WC);
        if(fault==1){errno=ENOMEM;return -1;}
        a->handle=42;return 0;
    }
    if(request==DRM_IOCTL_MSM_GEM_INFO){
        struct drm_msm_gem_info *a=arg;assert(a->handle==42 && a->info==5);
        if(fault==2){errno=EIO;return -1;}
        a->value=fault==3?MSM_BO_CACHED:MSM_BO_WC;return 0;
    }
    if(request==DRM_IOCTL_PRIME_HANDLE_TO_FD){
        struct drm_prime_handle *a=arg;assert(a->handle==42 && a->flags==(DRM_CLOEXEC|DRM_RDWR));
        if(fault==4){errno=EMFILE;return -1;}
        a->fd=prime_fd=open("/dev/null",O_RDWR|O_CLOEXEC);assert(prime_fd>=0);return 0;
    }
    if(request==DRM_IOCTL_GEM_CLOSE){
        assert(((struct drm_gem_close *)arg)->handle==42);closed++;
        if(fault==5){errno=EIO;return -1;}
        return 0;
    }
    assert(!"unexpected ioctl");return -1;
}
#include "../probe/graphics/quest2-system-ion.h"
int main(void){
    int control=open("/dev/null",O_RDONLY|O_CLOEXEC),result;assert(control>=0);
    assert(q2_system_ion_alloc(control,1,0,1U<<25,1,NULL)==-EINVAL);
    result=99;assert(q2_system_ion_alloc(control,0,0,1U<<25,1,&result)==-EINVAL && result==-1);
    const unsigned bad_masks[]={0,1,1U<<31,(1U<<25)|1};
    for(unsigned i=0;i<4;i++){
        result=99;assert(q2_system_ion_alloc(control,16384,0,bad_masks[i],1,&result)==-EOPNOTSUPP && result==-1);
    }
    const size_t bad_align[]={3,2*(size_t)sysconf(_SC_PAGESIZE),SIZE_MAX};
    for(unsigned i=0;i<3;i++)assert(q2_system_ion_alloc(control,16384,bad_align[i],1U<<25,1,&result)==-EOPNOTSUPP);
    assert(q2_system_ion_alloc(control,16384,0,1U<<25,2,&result)==-EOPNOTSUPP);
    assert(calls==0);
    const size_t good_align[]={0,1,64,(size_t)sysconf(_SC_PAGESIZE)};
    for(unsigned i=0;i<4;i++){
        assert(!q2_system_ion_alloc(control,16384,good_align[i],1U<<25,1,&result));
        assert(result>=0 && (fcntl(result,F_GETFD)&FD_CLOEXEC));assert(!close(result));
    }
    fault=1;assert(q2_system_ion_alloc(control,16384,0,1U<<25,1,&result)==-ENOMEM && result==-1);
    assert(q2_system_ion_alloc(-1,16384,0,1U<<25,0,&result)==-EBADF && result==-1);
    for(fault=0;fault<=5;fault++){
        closed=0;prime_fd=-1;result=-1;
        int r=q2_wc_allocate_on_fd(control,16384,&result);
        if(!fault){assert(!r && result==prime_fd);assert(!close(result));}
        else{
            assert(r<0 && result==-1);
            if(prime_fd>=0)assert(fcntl(prime_fd,F_GETFD)==-1 && errno==EBADF);
        }
        assert(closed==(fault==1?0u:1u));
    }
    assert(!close(control));puts("SYSTEM_ION_TEST_PASS");
}
