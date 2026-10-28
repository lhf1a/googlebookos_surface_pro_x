/* Allocation-only kernel capability check; no GPU submits or global changes. */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <drm/msm_drm.h>
#ifndef MSM_BO_CACHED_COHERENT
#define MSM_BO_CACHED_COHERENT 0x080000
#endif
int main(void) {
    int fd=open("/dev/dri/renderD128",O_RDWR|O_CLOEXEC);
    if(fd<0){perror("open render node");return 1;}
    struct drm_version version={0};char name[64]={0};
    version.name_len=sizeof(name)-1;version.name=name;
    if(ioctl(fd,DRM_IOCTL_VERSION,&version)||strcmp(name,"msm")){close(fd);return 1;}
    printf("MSM_VERSION %d.%d.%d\n",version.version_major,version.version_minor,version.version_patchlevel);
    const uint32_t modes[]={MSM_BO_WC,MSM_BO_CACHED,MSM_BO_CACHED_COHERENT};
    unsigned failures=0;
    for(unsigned i=0;i<3;i++) {
        struct drm_msm_gem_new request={.size=4096,.flags=modes[i]};
        int result=ioctl(fd,DRM_IOCTL_MSM_GEM_NEW,&request),error=result?errno:0;
        printf("MSM_CACHE_MODE flags=%x alloc_result=%d errno=%d\n",modes[i],result,error);
        if(result)failures++;
        if(!result){struct drm_gem_close release={.handle=request.handle};
            if(ioctl(fd,DRM_IOCTL_GEM_CLOSE,&release)){close(fd);return 1;}}
    }
    printf("MSM_CACHE_MODES_SUMMARY failed=%u gpu_coherence_tested=0\n",failures);
    close(fd);return failures?1:0;
}
