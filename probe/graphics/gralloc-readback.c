/* Test the donor HAL plus rebuilt MSM driver through the stable gralloc0 ABI. */
#include <hardware/gralloc.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    setvbuf(stdout,NULL,_IONBF,0);
    void *lib=dlopen(argc>1?argv[1]:"./gralloc.default.so",RTLD_NOW|RTLD_GLOBAL);
    if(!lib){fprintf(stderr,"dlopen: %s\n",dlerror());return 1;}
    gralloc_module_t *mod=dlsym(lib,HAL_MODULE_INFO_SYM_AS_STR);
    if(!mod){puts("No HAL module");return 1;}
    printf("GRALLOC_MODULE=%s\n",mod->common.name);
    alloc_device_t *dev=NULL;
    int rc=gralloc_open(&mod->common,&dev);
    if(rc || !dev){printf("gralloc_open=%d\n",rc);return 1;}
    int usage=GRALLOC_USAGE_HW_RENDER|GRALLOC_USAGE_HW_TEXTURE|GRALLOC_USAGE_SW_READ_OFTEN|GRALLOC_USAGE_SW_WRITE_OFTEN;
    for(int frame=0;frame<3;frame++){
        buffer_handle_t handle=NULL; int stride=0;
        rc=dev->alloc(dev,64,64,HAL_PIXEL_FORMAT_RGBA_8888,usage,&handle,&stride);
        printf("GRALLOC_ALLOC frame=%d rc=%d stride=%d fds=%d\n",frame,rc,stride,handle?handle->numFds:-1);
        if(rc || !handle || stride<64)return 1;
        void *addr=NULL;
        rc=mod->lock(mod,handle,GRALLOC_USAGE_SW_WRITE_OFTEN,0,0,64,64,&addr);
        if(rc || !addr){printf("write lock=%d\n",rc);return 1;}
        for(int y=0;y<64;y++)for(int x=0;x<64;x++)((uint32_t*)addr)[y*stride+x]=0xff000000u|((uint32_t)frame<<16)|(y<<8)|x;
        if(mod->unlock(mod,handle))return 1;
        addr=NULL; rc=mod->lock(mod,handle,GRALLOC_USAGE_SW_READ_OFTEN,0,0,64,64,&addr);
        if(rc || !addr){printf("read lock=%d\n",rc);return 1;}
        for(int y=0;y<64;y++)for(int x=0;x<64;x++)if(((uint32_t*)addr)[y*stride+x]!=(0xff000000u|((uint32_t)frame<<16)|(y<<8)|x)){puts("GRALLOC_PIXEL_MISMATCH");return 1;}
        if(mod->unlock(mod,handle)||dev->free(dev,handle))return 1;
        printf("GRALLOC_FRAME_OK frame=%d pixels=4096\n",frame);
    }
    gralloc_close(dev); puts("GRALLOC_READBACK_PASS"); return 0;
}
