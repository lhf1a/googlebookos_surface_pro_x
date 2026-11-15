// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <android/hardware/graphics/mapper/IMapper.h>
#include <android/hardware/graphics/mapper/utils/IMapperMetadataTypes.h>
#include <android/hardware_buffer.h>
#include <dlfcn.h>
#include <stdexcept>
#include <vector>
namespace spx {
inline AIMapper* mapper() {
    static AIMapper* result=[] {
        void* support=dlopen("libvndksupport.so",RTLD_NOW|RTLD_LOCAL);
        auto load=support?reinterpret_cast<void*(*)(const char*,int)>(
                            dlsym(support,"android_load_sphal_library")):nullptr;
        // Match Hybrid's allocator getIMapperLibrarySuffix and libui's sphal registry.
        void* library=load?load("mapper.quest2.so",RTLD_NOW|RTLD_LOCAL):nullptr;
        auto entry=library?reinterpret_cast<AIMapper_Error(*)(AIMapper**)>(
                              dlsym(library,"AIMapper_loadIMapper")):nullptr;
        AIMapper* m=nullptr;
        if(!entry || entry(&m)!=AIMAPPER_ERROR_NONE || !m || m->version<5)
            throw std::runtime_error("load Hybrid mapper5");
        return m;
    }();return result;
}
template<aidl::android::hardware::graphics::common::StandardMetadataType T>
auto bufferMetadata(buffer_handle_t h) {
    auto* m=mapper();int32_t size=m->v5.getStandardMetadata(h,int64_t(T),nullptr,0);
    if(size<0)throw std::runtime_error("query buffer metadata");
    std::vector<uint8_t> bytes(size);
    if(m->v5.getStandardMetadata(h,int64_t(T),bytes.data(),size)!=size)
        throw std::runtime_error("read buffer metadata");
    auto result=android::hardware::graphics::mapper::StandardMetadata<T>::value::decode(bytes.data(),bytes.size());
    if(!result)throw std::runtime_error("decode buffer metadata");
    return *result;
}
inline AHardwareBuffer_Desc describeBuffer(const native_handle_t* raw) {
    using T=aidl::android::hardware::graphics::common::StandardMetadataType;
    struct Import {
        buffer_handle_t h=nullptr;
        ~Import(){if(h)mapper()->v5.freeBuffer(h);}
    } imported;
    if(mapper()->v5.importBuffer(raw,&imported.h)!=AIMAPPER_ERROR_NONE)
        throw std::runtime_error("import Hybrid mapper handle");
    AHardwareBuffer_Desc d{};
    auto w=bufferMetadata<T::WIDTH>(imported.h),h=bufferMetadata<T::HEIGHT>(imported.h);
    auto stride=bufferMetadata<T::STRIDE>(imported.h);
    if(w>UINT32_MAX || h>UINT32_MAX || stride>UINT32_MAX)
        throw std::runtime_error("buffer dimensions overflow");
    d.width=w;d.height=h;d.stride=stride;
    d.layers=bufferMetadata<T::LAYER_COUNT>(imported.h);
    d.format=uint32_t(bufferMetadata<T::PIXEL_FORMAT_REQUESTED>(imported.h));
    d.usage=uint64_t(bufferMetadata<T::USAGE>(imported.h));
    return d;
}
}
