/* Private probe-only diagnostic preload. Never install into system processes. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>

void *dlopen(const char *name, int flags) {
    static void *(*real_open)(const char *, int);
    if (!real_open) real_open = (void *(*)(const char *, int))dlsym(RTLD_NEXT,"dlopen");
    if (!real_open) return NULL;
    void *handle = real_open(name,flags);
    fprintf(stderr,"QUEST2_DLOPEN name=%s flags=%x result=%p\n",name?name:"<null>",flags,handle);
    /* This diagnostic consumes the error only on failure; never used for a
     * passing compatibility result or normal operation. */
    if (!handle) fprintf(stderr,"QUEST2_DLOPEN_ERROR %s\n",dlerror());
    return handle;
}
