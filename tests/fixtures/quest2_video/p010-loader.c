#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    setbuf(stdout, NULL);
    if (argc != 2) return 64;
    void *handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!handle) { fprintf(stderr, "DLOPEN_ERROR=%s\n", dlerror()); return 2; }
    const char *symbols[] = {"sq2_p010_renderer_ctor", "sq2_p010_renderer_dtor",
                            "sq2_p010_renderer_render"};
    void *method = dlsym(handle, "_ZN7android10MediaCodec8setStateENS0_5StateE");
    Dl_info parent;
    assert(method && dladdr(method, &parent));
    assert(strcmp(parent.dli_fname, argv[1]) == 0);
    for (unsigned i = 0; i < 3; ++i) {
        void *target = dlsym(handle, symbols[i]);
        Dl_info child;
        assert(target && dladdr(target, &child));
        assert(strstr(child.dli_fname, "libsq2-p010-renderer.so"));
        void *bound = *(void **)((uint8_t *)parent.dli_fbase + 0x22b118 + i * 8);
        assert(bound == target);
        printf("BOUND %s=%p from=%s\n", symbols[i], target, child.dli_fname);
    }
    FILE *maps = fopen("/proc/self/maps", "r");
    assert(maps);
    char line[1024];
    while (fgets(line, sizeof(line), maps)) {
        if (strstr(line, "libstagefright.so") || strstr(line, "libsq2-p010-renderer.so"))
            fputs(line, stdout);
    }
    fclose(maps);
    assert(dlclose(handle) == 0);
    puts("RESULT=pass (linker/RELRO bindings only; no codec session)");
}
