/* Paired with the pinned GSL MSM coherent-cached allocation patch. The scope
 * follows Vulkan's actual HOST_CACHED properties without changing the public
 * memory type index, memory flags, requested size, or allocation result. */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
static thread_local unsigned cached_scope;
extern "C" unsigned sq2_gsl_cached_scope(unsigned enabled) {
    unsigned previous=cached_scope;cached_scope=enabled;return previous;
}
extern "C" int sq2_memory_alloc_pure_64(uint64_t size,uint64_t flags,void *out) {
    using Allocate=int(*)(uint64_t,uint64_t,void*);
    static Allocate original=[](){
        // This adapter lives in the donor's vendor namespace, unlike a system
        // LD_PRELOAD tracer. Resolve the same GSL instance already initialized.
        void *library=dlopen("/vendor/lib64/egl/libgsl.so",RTLD_NOW|RTLD_LOCAL);
        auto fn=library?reinterpret_cast<Allocate>(dlsym(library,"gsl_memory_alloc_pure_64")):nullptr;
        if(!fn){std::fputs("SQ2_GSL_CACHED_RESOLVE_FAILED\n",stderr);std::abort();}
        return fn;
    }();
    // GSL's default zero cache mode otherwise maps even HOST_CACHED Vulkan
    // allocations as WC. Explicit uncached modes retain their semantics.
    if(cached_scope && !(flags&0x0c000000ull))flags|=0x0c000000ull;
    return original(size,flags,out);
}
