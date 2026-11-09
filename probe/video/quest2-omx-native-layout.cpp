/* Negotiate against the same real gralloc backend that imports the ANW buffers. */
#include "quest2-omx-native-layout.h"
#include "quest2-omx-native-layout-check.h"
#include "../graphics/quest2-gralloc-backend.h"
#include <errno.h>
#include <android/log.h>
#include <mutex>
namespace {
struct NativeBackend { QuestGralloc gralloc;std::mutex mutex; };
NativeBackend &backend() { static NativeBackend state;return state; }
void log_geometry(const char *stage,unsigned w,unsigned h,uint64_t usage,
                  const QuestGeometry &g,int result,const uint32_t *pitches=nullptr,
                  const uint32_t *offsets=nullptr,unsigned planes=0) {
    __android_log_print(ANDROID_LOG_ERROR,"Q2NATIVE",
        "%s result=%d expected=%ux%u usage=%llx actual=%ux%u layers=%u format=%x "
        "usage=%llx stride=%u scan=%u size=%u yuv=%d ubwc=%d planar=%d sample=%u "
        "planes=%u pitch=%u,%u offset=%u,%u",
        stage,result,w,h,(unsigned long long)usage,g.width,g.height,g.layers,
        unsigned(g.format),(unsigned long long)g.usage,g.stride,g.scan,g.size,
        g.yuv,g.ubwc,g.planar_yv12,g.sample_bytes,planes,
        pitches?pitches[0]:0,pitches?pitches[1]:0,offsets?offsets[0]:0,offsets?offsets[1]:0);
    for(const auto *plane:{&g.plane,&g.chroma})
        __android_log_print(ANDROID_LOG_ERROR,"Q2NATIVE",
            "%s plane component=%u subsample=%u,%u offset=%u step=%d stride=%d "
            "stride_bytes=%d scan=%d size=%u",stage,plane->component,
            plane->h_subsampling,plane->v_subsampling,plane->offset,plane->step,
            plane->stride,plane->stride_bytes,plane->scanlines,plane->size);
}
bool geometry(QuestGralloc &gralloc,buffer_handle_t handle,unsigned w,unsigned h,
              unsigned format,uint64_t usage,QuestGeometry &g) {
    if(!gralloc.geometry(handle,g)) {
        log_geometry("backend_geometry",w,h,usage,g,-EINVAL);return false;
    }
    uint32_t pitches[4]={},offsets[4]={},planes=0;
    int result=gralloc::GetBufferLayout(reinterpret_cast<private_handle_t *>(
            const_cast<native_handle_t *>(handle)),pitches,offsets,&planes);
    if(result) {
        log_geometry("GetBufferLayout",w,h,usage,g,result,pitches,offsets,planes);return false;
    }
    bool valid=quest2_omx_native_geometry_valid(g,w,h,format,usage,pitches,offsets,planes);
    if(!valid)log_geometry("geometry_contract",w,h,usage,g,-EINVAL,pitches,offsets,planes);
    return valid;
}
}
extern "C" int quest2_omx_capture_layout_for_usage_v2(unsigned w,unsigned h,unsigned format,
    uint64_t usage,unsigned minimum_size,unsigned *stride,unsigned *scan,unsigned *size) {
    if(!stride || !scan || !size)return -EINVAL;
    *stride=*scan=*size=0;
    const unsigned grallocFormat=quest2_omx_native_gralloc_format(format);
    if(!grallocFormat || !quest2_omx_native_request_valid(w,h,usage))return -EINVAL;
    auto &state=backend();std::lock_guard<std::mutex> guard(state.mutex);
    auto &gralloc=state.gralloc;
    if(!gralloc.device)return -ENODEV;
    QuestGeometry g;unsigned allocation_height=h;
    for(;;) {
        buffer_handle_t handle=nullptr;
        if(gralloc.allocate(w,allocation_height,grallocFormat,usage,&handle) || !handle)return -ENOMEM;
        bool valid=geometry(gralloc,handle,w,allocation_height,grallocFormat,usage,g);
        int released=gralloc.release(handle);
        if(!valid || released)return -EINVAL;
        if(g.size>=minimum_size)break;
        const uint64_t row=g.plane.stride_bytes;
        uint64_t next=(2ULL*minimum_size+3*row-1)/(3*row);
        next=(next+31)&~31ULL;
        if(next<=allocation_height || next>4096)return -EINVAL;
        allocation_height=next;
    }
    *stride=g.plane.stride_bytes;*scan=g.scan;*size=g.size;
    printf("NATIVE_SURFACE_LAYOUT width=%u height=%u usage=%llx stride=%u scan=%u size=%u\n",
        w,h,(unsigned long long)usage,*stride,*scan,*size);
    return 0;
}
extern "C" int quest2_omx_capture_layout_for_usage(unsigned w,unsigned h,unsigned format,
    uint64_t usage,unsigned *stride,unsigned *scan,unsigned *size) {
    return quest2_omx_capture_layout_for_usage_v2(w,h,format,usage,0,stride,scan,size);
}
extern "C" int quest2_omx_capture_layout(unsigned w,unsigned h,unsigned format,
    unsigned *stride,unsigned *scan,unsigned *size) {
    return quest2_omx_capture_layout_for_usage_v2(w,h,format,0x20400b03ULL,0,stride,scan,size);
}
extern "C" int quest2_omx_validate_native_buffer(const native_handle_t *raw,
    unsigned w,unsigned h,unsigned format,uint64_t usage,
    unsigned stride,unsigned scan,unsigned size) {
    if(!quest2_omx_native_sample_bytes(format) || !quest2_omx_native_request_valid(w,h,usage) ||
       !stride || !scan || !size || !raw || raw->version!=sizeof(native_handle_t) ||
       raw->numFds!=2 || raw->numInts!=26 || raw->data[0]<0 || raw->data[1]<0) {
        __android_log_print(ANDROID_LOG_ERROR,"Q2NATIVE",
            "validate_request width=%u height=%u format=%x usage=%llx stride=%u scan=%u size=%u raw=%d",
            w,h,format,(unsigned long long)usage,stride,scan,size,raw!=nullptr);
        return -EINVAL;
    }
    auto &state=backend();std::lock_guard<std::mutex> guard(state.mutex);
    auto &gralloc=state.gralloc;
    if(!gralloc.device)return -ENODEV;
    native_handle_t *copy=native_handle_clone(raw);
    if(!copy)return -ENOMEM;
    int retained=gralloc.RETAIN(gralloc.device,copy);
    if(retained) {
        __android_log_print(ANDROID_LOG_ERROR,"Q2NATIVE","validate_retain result=%d",retained);
        native_handle_close(copy);native_handle_delete(copy);return -EINVAL;
    }
    QuestGeometry g;
    bool valid=geometry(gralloc,copy,w,h,format,usage,g);
    if(valid && (uint32_t(g.plane.stride_bytes)!=stride || g.scan!=scan || g.size!=size)) {
        __android_log_print(ANDROID_LOG_ERROR,"Q2NATIVE",
            "validate_session expected_stride=%u scan=%u size=%u actual_stride=%u scan=%u size=%u",
            stride,scan,size,g.stride,g.scan,g.size);
        valid=false;
    }
    int released=gralloc.release(copy);
    if(released)__android_log_print(ANDROID_LOG_ERROR,"Q2NATIVE","validate_release result=%d",released);
    return valid && !released?0:-EINVAL;
}
