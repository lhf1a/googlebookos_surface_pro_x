/* Inspect and exercise original donor YUV CPU layout without assuming QTI handle fields. */
#include <assert.h>
#include <dlfcn.h>
#include <hardware/gralloc1.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
extern void quest2_native_yuv_layout_check(const native_handle_t *,unsigned,unsigned,int,uint64_t);
#define LOAD(name) GRALLOC1_PFN_##name name=(GRALLOC1_PFN_##name)dev->getFunction(dev,GRALLOC1_FUNCTION_##name);assert(name)
static void fence_wait(int fd) {
    if(fd<0)return;
    struct pollfd p={.fd=fd,.events=POLLIN};int r;
    do{r=poll(&p,1,5000);}while(r<0 && errno==EINTR);
    assert(r==1 && (p.revents&POLLIN) && !(p.revents&(POLLERR|POLLNVAL)));
    assert(!close(fd));
}
#include "quest2-native-p010-probe.h"
int main(int argc,char **argv) {
    if((argc!=3 && argc!=4 && argc!=6) || argv[1][0]!='/')return 64;
    int gpu=argc>=4 && !strcmp(argv[3],"--gpu");
    int geometry=argc>=4 && !strcmp(argv[3],"--geometry");
    if(argc>=4 && !gpu && !geometry)return 64;
    char *end=NULL;unsigned long value=strtoul(argv[2],&end,0);
    if(!*argv[2] || *end || (value!=0x23 && value!=0x36 && value!=0x7fa30c0a))return 64;
    int32_t format=(int32_t)value;
    if(gpu && format!=0x36 && format!=0x7fa30c0a)return 64;
    setvbuf(stdout,NULL,_IONBF,0);
    void *library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!library){fprintf(stderr,"YUV_LOAD_FAIL %s\n",dlerror());return 1;}
    void *layout_library=dlopen("libadreno_utils.so",RTLD_NOW|RTLD_LOCAL);assert(layout_library);
    hw_module_t *module=dlsym(library,HAL_MODULE_INFO_SYM_AS_STR);assert(module);
    gralloc1_device_t *dev=NULL;assert(!gralloc1_open(module,&dev));
    LOAD(CREATE_DESCRIPTOR);LOAD(DESTROY_DESCRIPTOR);LOAD(SET_DIMENSIONS);LOAD(SET_FORMAT);
    LOAD(SET_LAYER_COUNT);LOAD(SET_PRODUCER_USAGE);LOAD(SET_CONSUMER_USAGE);LOAD(ALLOCATE);
    uint32_t capability_count=0;dev->getCapabilities(dev,&capability_count,NULL);
    assert(capability_count<=32);int32_t capabilities[32]={0};dev->getCapabilities(dev,&capability_count,capabilities);
    int deletes=0;for(uint32_t i=0;i<capability_count;i++)if(capabilities[i]==GRALLOC1_CAPABILITY_RELEASE_IMPLY_DELETE)deletes=1;
    assert(deletes); /* This probe's pinned donor owns handle deletion. */
    LOAD(GET_NUM_FLEX_PLANES);LOAD(LOCK_FLEX);LOAD(UNLOCK);LOAD(RELEASE);
    unsigned w=128,h=72;
    if(argc==6) {
        unsigned long width=strtoul(argv[4],&end,10);if(!*argv[4] || *end || width<128 || width>4096 || width%2)return 64;
        unsigned long height=strtoul(argv[5],&end,10);if(!*argv[5] || *end || height<8 || height>4096 || height%2)return 64;
        w=(unsigned)width;h=(unsigned)height;
    }
    gralloc1_buffer_descriptor_t descriptor=0;assert(!CREATE_DESCRIPTOR(dev,&descriptor));
    assert(!SET_DIMENSIONS(dev,descriptor,w,h));assert(!SET_FORMAT(dev,descriptor,format));
    assert(!SET_LAYER_COUNT(dev,descriptor,1));
    uint64_t producer=GRALLOC1_PRODUCER_USAGE_CPU_WRITE_OFTEN;
    uint64_t consumer=GRALLOC1_CONSUMER_USAGE_CPU_READ_OFTEN|GRALLOC1_CONSUMER_USAGE_GPU_TEXTURE;
    assert(!SET_PRODUCER_USAGE(dev,descriptor,producer));assert(!SET_CONSUMER_USAGE(dev,descriptor,consumer));
    buffer_handle_t handle=NULL;int error=ALLOCATE(dev,1,&descriptor,&handle);
    assert(!DESTROY_DESCRIPTOR(dev,descriptor));
    printf("YUV_ALLOC format=0x%x result=%d\n",format,error);
    if(error || !handle)return 1;
    assert(handle->numFds>=1);
    struct stat st;assert(!fstat(handle->data[0],&st) && st.st_size>0);
    quest2_native_yuv_layout_check(handle,w,h,format,producer|consumer);
    if(gpu || geometry) {
        if(gpu)p010_gpu_check(dev,handle,w,h,format,producer|consumer);
        assert(!RELEASE(dev,handle));assert(!gralloc1_close(dev));
        dlclose(layout_library);dlclose(library);return 0;
    }
    uint32_t count=0;error=GET_NUM_FLEX_PLANES(dev,handle,&count);
    printf("YUV_FLEX_COUNT result=%d planes=%u fd_bytes=%lld\n",error,count,(long long)st.st_size);
    assert(!error && count==3);
    android_flex_plane_t planes[3]={0};android_flex_layout_t layout={.planes=planes};
    gralloc1_rect_t rect={0,0,w,h};
    error=LOCK_FLEX(dev,handle,producer,GRALLOC1_CONSUMER_USAGE_CPU_READ_OFTEN,&rect,&layout,-1);
    printf("YUV_LOCK format=0x%x result=%d layout=%u planes=%u\n",format,error,layout.format,layout.num_planes);
    assert(!error && layout.format==FLEX_FORMAT_YCbCr && layout.num_planes==3);
    uintptr_t base=(uintptr_t)planes[0].top_left;assert(base);
    unsigned depth=format==0x23?8:10,container=depth==8?8:16;
    for(unsigned i=0;i<3;i++) {
        android_flex_plane_t *plane=&planes[i];
        printf("YUV_PLANE index=%u component=%u offset=%lld bits=%d used=%d step=%d stride=%d subsample=%d,%d\n",
            i,plane->component,(long long)((uintptr_t)plane->top_left-base),plane->bits_per_component,plane->bits_used,
            plane->h_increment,plane->v_increment,plane->h_subsampling,plane->v_subsampling);
        assert(plane->component==(1u<<i) && plane->bits_per_component==(int)container && plane->bits_used==(int)depth);
        assert(plane->h_subsampling==(i?2:1) && plane->v_subsampling==(i?2:1));
        unsigned width=w/plane->h_subsampling,height=h/plane->v_subsampling;
        assert(plane->h_increment>0 && plane->v_increment>0 && (uintptr_t)plane->top_left>=base);
        size_t last=(uintptr_t)plane->top_left-base+(height-1)*(size_t)plane->v_increment+(width-1)*(size_t)plane->h_increment+container/8;
        assert(last<=(size_t)st.st_size);
        for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++) {
            uint16_t sample=(uint16_t)(((x*17+y*31+i*53)&((1u<<depth)-1))<<(container-depth));
            memcpy(plane->top_left+y*plane->v_increment+x*plane->h_increment,&sample,container/8);
        }
    }
    int fence=-1;assert(!UNLOCK(dev,handle,&fence));fence_wait(fence);
    memset(planes,0,sizeof(planes));layout=(android_flex_layout_t){.planes=planes};
    assert(!LOCK_FLEX(dev,handle,0,GRALLOC1_CONSUMER_USAGE_CPU_READ_OFTEN,&rect,&layout,-1));
    assert(layout.format==FLEX_FORMAT_YCbCr && layout.num_planes==3);
    for(unsigned i=0;i<3;i++) {
        android_flex_plane_t *plane=&planes[i];
        assert(plane->component==(1u<<i) && plane->bits_per_component==(int)container && plane->bits_used==(int)depth);
        assert(plane->h_subsampling==(i?2:1) && plane->v_subsampling==(i?2:1));
        for(unsigned y=0;y<h/(i?2:1);y++)for(unsigned x=0;x<w/(i?2:1);x++) {
            uint16_t sample=0;memcpy(&sample,plane->top_left+y*plane->v_increment+x*plane->h_increment,container/8);
            assert(sample==(uint16_t)(((x*17+y*31+i*53)&((1u<<depth)-1))<<(container-depth)));
        }
    }
    fence=-1;assert(!UNLOCK(dev,handle,&fence));fence_wait(fence);
    assert(!RELEASE(dev,handle));assert(!gralloc1_close(dev));
    dlclose(layout_library);dlclose(library);
    printf("YUV_LAYOUT_PASS format=0x%x depth=%u cpu_roundtrip=1\n",format,depth);
    return 0;
}
