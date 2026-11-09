/* Layout comes from the same Stable-C mapper used by unmodified system libui. */
#include "quest2-mapper5-video-buffer.h"
#include <android/hardware/graphics/mapper/IMapper.h>
#include <android/hardware/graphics/mapper/utils/IMapperMetadataTypes.h>
#include <system/window.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <dlfcn.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
namespace gc=aidl::android::hardware::graphics::common;
namespace mt=android::hardware::graphics::mapper;
using Type=gc::StandardMetadataType;
static AIMapper *mapper;
template<Type T> static auto metadata(buffer_handle_t b) {
    int32_t size=mapper->v5.getStandardMetadata(b,int64_t(T),nullptr,0);assert(size>=0);
    std::vector<uint8_t> data(size);
    assert(mapper->v5.getStandardMetadata(b,int64_t(T),data.data(),data.size())==size);
    auto decoded=mt::StandardMetadata<T>::value::decode(data.data(),data.size());assert(decoded);return *decoded;
}
static int describe_video_buffer(quest2_video_buffer *out) {
    if(!mapper) {
        void *support=dlopen("libvndksupport.so",RTLD_NOW|RTLD_LOCAL);assert(support);
        auto load=reinterpret_cast<void *(*)(const char *,int)>(dlsym(support,"android_load_sphal_library"));assert(load);
        void *library=load("mapper.quest2.so",RTLD_NOW|RTLD_LOCAL);assert(library);
        auto entry=reinterpret_cast<AIMapper_Error (*)(AIMapper **)>(dlsym(library,"AIMapper_loadIMapper"));assert(entry);
        assert(entry(&mapper)==AIMAPPER_ERROR_NONE && mapper->version==AIMAPPER_VERSION_5);
    }
    AHardwareBuffer_Desc desc{};
    AHardwareBuffer_describe(out->ahb,&desc);out->stride=desc.stride;
    auto client=reinterpret_cast<PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC>(eglGetProcAddress("eglGetNativeClientBufferANDROID"));assert(client);
    auto *native=static_cast<ANativeWindowBuffer_t *>(client(out->ahb));assert(native && native->handle);
    out->handle=native->handle;
    buffer_handle_t imported=nullptr;
    assert(mapper->v5.importBuffer(out->handle,&imported)==AIMAPPER_ERROR_NONE);
    assert(metadata<Type::WIDTH>(imported)==desc.width && metadata<Type::HEIGHT>(imported)==desc.height &&
           int(metadata<Type::PIXEL_FORMAT_REQUESTED>(imported))==int(desc.format) &&
           metadata<Type::STRIDE>(imported)==desc.stride &&
           metadata<Type::PIXEL_FORMAT_MODIFIER>(imported)==0 &&
           metadata<Type::PROTECTED_CONTENT>(imported)==0);
    out->usage=uint64_t(metadata<Type::USAGE>(imported));
    printf("MAPPER5_VIDEO_USAGE descriptor=%llx allocation=%llx\n",
           (unsigned long long)desc.usage,(unsigned long long)out->usage);
    // Pinned libui strips these two legacy ANW hints before allocation.
    assert(out->usage==desc.usage || out->usage==(desc.usage&~0x2400ULL));
    out->capacity=metadata<Type::ALLOCATION_SIZE>(imported);
    auto planes=metadata<Type::PLANE_LAYOUTS>(imported);assert(planes.size()==2);
    assert(!planes[0].offsetInBytes && planes[0].strideInBytes>0 &&
           planes[0].strideInBytes<=UINT32_MAX && planes[1].strideInBytes>0 &&
           planes[1].strideInBytes<=UINT32_MAX && planes[1].offsetInBytes>0 &&
           planes[1].offsetInBytes<=UINT32_MAX &&
           uint64_t(planes[1].offsetInBytes+planes[1].totalSizeInBytes)<=out->capacity);
    out->ystride=planes[0].strideInBytes;out->cstride=planes[1].strideInBytes;out->uv=planes[1].offsetInBytes;
    assert(mapper->v5.freeBuffer(imported)==AIMAPPER_ERROR_NONE);
    quest2_video_buffer_check_maps();return 0;
}
extern "C" int quest2_video_buffer_allocate(unsigned w,unsigned h,int format,uint64_t usage,quest2_video_buffer *out) {
    assert(out && !out->ahb);
    AHardwareBuffer_Desc desc{};desc.width=w;desc.height=h;desc.layers=1;desc.format=format;desc.usage=usage;
    int error=AHardwareBuffer_allocate(&desc,&out->ahb);
    printf("MAPPER5_VIDEO_AHB_ALLOC w=%u h=%u format=%x usage=%llx error=%d\n",w,h,format,(unsigned long long)usage,error);
    if(error)return error;
    AHardwareBuffer_describe(out->ahb,&desc);
    assert(desc.width==w && desc.height==h && desc.format==unsigned(format) && desc.usage==usage);
    int described=describe_video_buffer(out);
    assert(out->usage==usage);return described;
}
extern "C" int quest2_video_buffer_import(AHardwareBuffer *buffer,quest2_video_buffer *out) {
    assert(buffer && out && !out->ahb);
    AHardwareBuffer_acquire(buffer);out->ahb=buffer;
    return describe_video_buffer(out);
}
extern "C" void quest2_video_buffer_release(quest2_video_buffer *buffer) {
    if(buffer->ahb)AHardwareBuffer_release(buffer->ahb);
    *buffer={};
}
extern "C" void quest2_video_buffer_check_maps() {
    FILE *file=fopen("/proc/self/maps","r");assert(file);char line[2048];bool found=false;
    while(fgets(line,sizeof(line),file)) {
        assert(!strstr(line,"-impl-quest2.so") && !strstr(line,"-impl-qti-display.so") &&
               !strstr(line,"libquest2-native-mapper") && !strstr(line,"libquest2-original-mapper") &&
               !strstr(line,"libquest2-v77-ahb.so") && !strstr(line,"libquest2-egl-wrapper.so") &&
               !strstr(line,"mapper.minigbm.so"));
        if(strstr(line,"mapper.quest2.so"))found=true;
    }
    fclose(file);assert(found);
}
