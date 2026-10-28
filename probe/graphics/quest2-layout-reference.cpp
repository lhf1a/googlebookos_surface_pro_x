/* Private donor layout query. No EGL context, command submission or system
 * replacement. Function ABI is checked against the pinned v77 grallocutils
 * wrappers and the QTI headers in the pinned AdrenoTools source. */
#include <android/hardware_buffer.h>
#include <cstdio>
#include <cstdlib>
#include <cinttypes>
#include "quest2-mapper5.h"
extern "C" const native_handle_t *AHardwareBuffer_getNativeHandle(const AHardwareBuffer *);

int main() {
    setvbuf(stdout,nullptr,_IONBF,0);
    void *lib=dlopen("libadreno_utils.so",RTLD_NOW|RTLD_LOCAL);
    if(!lib){std::fprintf(stderr,"dlopen: %s\n",dlerror());return 1;}
    auto blob_size=reinterpret_cast<uint32_t(*)()>(dlsym(lib,"adreno_get_metadata_blob_size"));
    auto init_layout=reinterpret_cast<int(*)(void*,int,int,int,int,int,int,uint64_t,uint32_t)>(
        dlsym(lib,"adreno_init_memory_layout"));
    auto buffer_size=reinterpret_cast<uint32_t(*)(void*)>(dlsym(lib,"adreno_get_aligned_gpu_buffer_size"));
    auto align=reinterpret_cast<void(*)(int,int,int,int,int,int,int,int,int*,int*)>(
        dlsym(lib,"compute_fmt_aligned_width_and_height"));
    if(!blob_size||!init_layout||!buffer_size||!align){std::fprintf(stderr,"missing donor layout API\n");return 1;}
    Dl_info origin={};
    if(!dladdr(reinterpret_cast<void*>(blob_size),&origin)||!origin.dli_fname){return 1;}
    std::printf("DONOR_LAYOUT_LIBRARY=%s\n",origin.dli_fname);
    const uint32_t bytes=blob_size();
    // Exact v77 value confirmed at VA 0x10eb8, not a general driver ABI.
    if(bytes!=5528){std::fprintf(stderr,"unexpected blob bytes=%u\n",bytes);return 1;}
    std::printf("DONOR_LAYOUT_BLOB_BYTES=%u\n",bytes);
    constexpr uint64_t usage=AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN|
        AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN|AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT|
        AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;
    const uint32_t sizes[][2]={{64,64},{13,7},{257,129},{1920,1280},{2880,1920}};
    for(const auto &size:sizes){
        AHardwareBuffer_Desc desc={};desc.width=size[0];desc.height=size[1];
        desc.layers=1;desc.format=AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;desc.usage=usage;
        AHardwareBuffer *ahb=nullptr;
        if(AHardwareBuffer_allocate(&desc,&ahb)){return 1;}
        sq2::BufferInfo info;
        if(!sq2::color_info(AHardwareBuffer_getNativeHandle(ahb),info)){
            AHardwareBuffer_release(ahb);return 1;
        }
        // Tile mode 0 (linear), RGBA8 Adreno format 28, one sample/plane.
        int aligned_w=0,aligned_h=0;
        align(size[0],size[1],0,28,1,0,0,512,&aligned_w,&aligned_h);
        void *blob=std::calloc(1,bytes);
        if(!blob){AHardwareBuffer_release(ahb);return 1;}
        int rc=init_layout(blob,size[0],size[1],1,28,1,0,usage,1);
        std::printf("LAYOUT width=%u height=%u usage=0x%" PRIx64
            " donor_align=%dx%d donor_rc=%d donor_bytes=%u minigbm_stride=%" PRIu64
            " minigbm_offset=%" PRIu64 " minigbm_plane=%" PRIu64 " minigbm_fd=%" PRIu64 "\n",
            size[0],size[1],usage,aligned_w,aligned_h,rc,rc==0?buffer_size(blob):0,
            info.stride,info.offset,info.plane_size,info.fd_size);
        std::free(blob);AHardwareBuffer_release(ahb);
        if(rc){return 1;}
    }
    dlclose(lib);
    puts("QUEST2_LAYOUT_REFERENCE_OK no-EGL-no-draw; sizes alone do not prove compatibility");
    return 0;
}
