/* Transparent tracing of the donor's property-name map; no values or DRM replies are changed. */
#include <string>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
static constexpr const char *symbol=
    "_ZNK7sde_drm18DRMPropertyManager15GetPropertyEnumERKNSt3__112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE";
extern "C" unsigned property_enum(const void *,const std::string &)
    asm("_ZNK7sde_drm18DRMPropertyManager15GetPropertyEnumERKNSt3__112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE");
extern "C" unsigned property_enum(const void *self,const std::string &name) {
    using Query=unsigned (*)(const void *,const std::string &);
    static void *library=dlopen("libsdedrm.so",RTLD_NOW|RTLD_NOLOAD);
    static Query next=library?reinterpret_cast<Query>(dlsym(library,symbol)):nullptr;
    if(!next){fprintf(stderr,"QTI_DRM_PROPERTY_ENUM_SYMBOL_FAILED %s\n",dlerror());std::abort();}
    static bool checked=false;
    if(!checked) {
        checked=true;
        for(const char *key:{"capabilities","IN_FORMATS","type","scaler_v2","csc_v1","input_fence","IN_FENCE_FD"})
            printf("QTI_DRM_ENUM_CONTRACT name=%s enum=%u\n",key,next(self,std::string(key)));
    }
    unsigned value=next(self,name);
    printf("QTI_DRM_ENUM name=%s enum=%u\n",name.c_str(),value);
    return value;
}
