/* Use the same namespace selection as the actual QC2Mapper5Helper constructor.
 * A direct DT_NEEDED on mapper.quest2.so would create a second default-namespace
 * registry when libui has already loaded the HAL into sphal. Keep HAL/support
 * handles alive for the lifetime of their returned function table. */
#pragma once
#include <dlfcn.h>
#include <android/hardware/graphics/mapper/IMapper.h>
inline AIMapper_Error quest2LoadNativeMapper(AIMapper **out) {
    if(!out)return AIMAPPER_ERROR_BAD_VALUE;
    *out=nullptr;
    using Loader=AIMapper_Error(*)(AIMapper **);
    static Loader entry=[]() -> Loader {
        void *support=dlopen("libvndksupport.so",RTLD_NOW|RTLD_LOCAL);
        if(!support)return nullptr;
        auto sphal=reinterpret_cast<void*(*)(const char*,int)>(dlsym(support,"android_load_sphal_library"));
        if(!sphal)return nullptr;
        void *library=sphal("mapper.quest2.so",RTLD_NOW);
        if(!library)return nullptr;
        return reinterpret_cast<Loader>(dlsym(library,"AIMapper_loadIMapper"));
    }();
    return entry?entry(out):AIMAPPER_ERROR_NO_RESOURCES;
}
