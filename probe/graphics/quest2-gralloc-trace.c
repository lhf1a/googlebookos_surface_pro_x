/* Optional process-local LD_PRELOAD tracing. Never substitutes handles or
 * metadata; use only with the independent donor probe, not system services. */
#include <hardware/hardware.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>

int hw_get_module(const char *id,const hw_module_t **module) {
    typedef int (*get_t)(const char *,const hw_module_t **);
    /* The donor is RTLD_LOCAL, so its dependency may not be in RTLD_NEXT's
     * global lookup scope. Resolve from the explicit platform library. */
    void *lib=dlopen("libhardware.so",RTLD_NOW|RTLD_LOCAL);
    get_t next=lib ? (get_t)dlsym(lib,"hw_get_module") : NULL;
    if(!next) { fprintf(stderr,"TRACE hw_get_module missing next\n");return -ENOSYS; }
    int result=next(id,module);
    fprintf(stderr,"TRACE hw_get_module id=%s result=%d",id,result);
    if(!result && module && *module)
        fprintf(stderr," module=%s name=%s version=%u",(*module)->id,(*module)->name,(*module)->module_api_version);
    fputc('\n',stderr);
    dlclose(lib);
    return result;
}
