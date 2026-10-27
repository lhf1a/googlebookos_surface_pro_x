#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>
#include <sys/mman.h>
#include <drm.h>
#include <msm_drm.h>
/* Submit only an Adreno type-7 CP_NOP, then wait for the hardware fence.
 * Header encoding follows adreno_gpu.h OUT_PKT7; this is not a 3D draw test. */
static int submit_nop(int fd, unsigned handle) {
    struct drm_msm_gem_info info={.handle=handle,.info=MSM_INFO_GET_OFFSET};
    if(ioctl(fd,DRM_IOCTL_MSM_GEM_INFO,&info)<0){perror("GEM_INFO");return 0;}
    uint32_t *map=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_SHARED,fd,info.value);
    if(map==MAP_FAILED){perror("GEM_MMAP");return 0;}
    map[0]=0x70100001; map[1]=0; /* CP_NOP with one ignored payload dword */
    __sync_synchronize();
    struct drm_msm_gem_submit_bo bo={.flags=MSM_SUBMIT_BO_READ,.handle=handle};
    struct drm_msm_gem_submit_cmd cmd={.type=MSM_SUBMIT_CMD_BUF,.size=8};
    int ok=1;
    for(int i=0;i<3;i++) {
        struct drm_msm_gem_submit s={.flags=MSM_PIPE_3D0,.nr_bos=1,.nr_cmds=1,
            .bos=(uintptr_t)&bo,.cmds=(uintptr_t)&cmd};
        if(ioctl(fd,DRM_IOCTL_MSM_GEM_SUBMIT,&s)<0){perror("GPU_SUBMIT");ok=0;break;}
        struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
        struct drm_msm_wait_fence w={.fence=s.fence,.timeout={.tv_sec=now.tv_sec+5,.tv_nsec=now.tv_nsec}};
        if(ioctl(fd,DRM_IOCTL_MSM_WAIT_FENCE,&w)<0){perror("GPU_WAIT_FENCE");ok=0;break;}
        printf("GPU_NOP_FENCE_OK iteration=%d fence=%u\n",i+1,s.fence);
    }
    munmap(map,4096);return ok;
}
int main(void) {
    setvbuf(stdout,NULL,_IOLBF,0);
    int found=0,ok=0,submitted=0;
    for(int i=128;i<144;i++) {
        char path[64],name[128]={0}; snprintf(path,sizeof(path),"/dev/dri/renderD%d",i);
        int fd=open(path,O_RDWR); if(fd<0)continue; found++;
        struct drm_version v={.name_len=sizeof(name)-1,.name=name};
        if(ioctl(fd,DRM_IOCTL_VERSION,&v)<0) {perror("DRM_VERSION");close(fd);continue;}
        printf("DEVICE %s driver=%s version=%d.%d.%d\n",path,name,v.version_major,v.version_minor,v.version_patchlevel);
        if(strcmp(name,"msm")) {close(fd);continue;}
        int params=0;
        for(unsigned param=MSM_PARAM_GPU_ID;param<=MSM_PARAM_CHIP_ID;param++) {
            struct drm_msm_param p={.pipe=MSM_PIPE_3D0,.param=param};
            if(ioctl(fd,DRM_IOCTL_MSM_GET_PARAM,&p)==0) {printf("PARAM id=%u value=%llu hex=0x%llx\n",param,(unsigned long long)p.value,(unsigned long long)p.value);params++;}
            else printf("PARAM_ERROR id=%u errno=%d %s\n",param,errno,strerror(errno));
        }
        struct drm_msm_gem_new b={.size=4096,.flags=MSM_BO_WC};
        if(ioctl(fd,DRM_IOCTL_MSM_GEM_NEW,&b)==0) {
            printf("GEM_ALLOCATION_OK handle=%u bytes=4096\n",b.handle);
            submitted+=submit_nop(fd,b.handle);
            struct drm_gem_close c={.handle=b.handle}; ioctl(fd,DRM_IOCTL_GEM_CLOSE,&c); if(params==3)ok++;
        } else perror("GEM_NEW");
        close(fd);
    }
    printf("RESULT render_nodes=%d msm_parameter_and_allocation_ok=%d gpu_nop_submission_ok=%d; 3D rendering and visual display correctness NOT tested\n",found,ok,submitted);
    return ok&&submitted?0:2;
}
