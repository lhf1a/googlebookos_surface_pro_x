/* Private process-only OMX discovery. No service registration or decode. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <OMX_Component.h>
_Static_assert(sizeof(OMX_U32) == 4, "OMX_U32 ABI");
_Static_assert(sizeof(void *) == 8, "64-bit probe required");
static OMX_ERRORTYPE event(OMX_HANDLETYPE h, OMX_PTR app, OMX_EVENTTYPE e,
                          OMX_U32 a, OMX_U32 b, OMX_PTR data) {
    (void)h; (void)app; (void)data;
    printf("EVENT event=%u data1=0x%08x data2=0x%08x\n", (unsigned)e, a, b);
    return OMX_ErrorNone;
}
static OMX_ERRORTYPE empty(OMX_HANDLETYPE h, OMX_PTR app, OMX_BUFFERHEADERTYPE *b) {
    (void)h; (void)app; (void)b; puts("UNEXPECTED_EMPTY_BUFFER_DONE"); return OMX_ErrorNone;
}
static OMX_ERRORTYPE fill(OMX_HANDLETYPE h, OMX_PTR app, OMX_BUFFERHEADERTYPE *b) {
    (void)h; (void)app; (void)b; puts("UNEXPECTED_FILL_BUFFER_DONE"); return OMX_ErrorNone;
}
static void maps(void) {
    FILE *f = fopen("/proc/self/maps", "r"); char line[2048];
    if (!f) return;
    while (fgets(line, sizeof(line), f))
        if (strstr(line, ".so") && (strstr(line, "Omx") || strstr(line, "vendor/") ||
            strstr(line, "libion") || strstr(line, "libplatformconfig") || strstr(line, "libc2d")))
            printf("MAP %s", line);
    fclose(f);
}
static void *symbol(void *library, const char *name) {
    dlerror(); void *p = dlsym(library, name); const char *error = dlerror();
    if (error) { fprintf(stderr, "SYMBOL_ERROR %s: %s\n", name, error); return NULL; }
    Dl_info info;
    printf("SYMBOL %s=%p owner=%s\n", name, p,
           dladdr(p, &info) && info.dli_fname ? info.dli_fname : "unknown");
    return p;
}
#ifdef QUEST2_NATIVE_IMPORT
extern int quest2_omx_native_import(OMX_HANDLETYPE, OMX_COMPONENTTYPE *, OMX_PARAM_PORTDEFINITIONTYPE *, uint32_t);
#endif
static int native_parameters(OMX_HANDLETYPE handle, OMX_COMPONENTTYPE *component) {
    if(!component->GetExtensionIndex || !component->GetParameter || !component->SetParameter)return 10;
    const char *names[] = {
        "OMX.google.android.index.enableAndroidNativeBuffers",
        "OMX.google.android.index.useAndroidNativeBuffer2",
        "OMX.google.android.index.getAndroidNativeBufferUsage"
    };
    OMX_INDEXTYPE indices[3] = {0};
    for (unsigned i=0;i<3;i++) {
        OMX_ERRORTYPE e=component->GetExtensionIndex(handle,(OMX_STRING)names[i],&indices[i]);
        printf("NATIVE_EXTENSION name=%s result=0x%08x index=0x%08x\n",names[i],(unsigned)e,(unsigned)indices[i]);
        if(e!=OMX_ErrorNone)return 10;
    }
    /* Fixed-width Android HardwareAPI parameter ABI, including OMX version. */
    struct {OMX_U32 nSize;OMX_VERSIONTYPE nVersion;OMX_U32 nPortIndex;OMX_BOOL enable;} enable={0};
    _Static_assert(sizeof(enable)==16,"Android native enable ABI");
    enable.nSize=sizeof(enable);enable.nVersion.nVersion=0x00000101;enable.nPortIndex=1;enable.enable=OMX_TRUE;
    OMX_ERRORTYPE e=component->SetParameter(handle,indices[0],&enable);
    printf("NATIVE_ENABLE result=0x%08x\n",(unsigned)e);
    if(e!=OMX_ErrorNone)return 11;
    OMX_VIDEO_PARAM_PORTFORMATTYPE format={0};
    format.nSize=sizeof(format);format.nVersion=enable.nVersion;format.nPortIndex=1;
    format.eColorFormat=(OMX_COLOR_FORMATTYPE)0x7fa30c04;
    e=component->SetParameter(handle,OMX_IndexParamVideoPortFormat,&format);
    printf("NATIVE_LINEAR_FORMAT result=0x%08x\n",(unsigned)e);
    if(e!=OMX_ErrorNone)return 12;
    struct {OMX_U32 nSize;OMX_VERSIONTYPE nVersion;OMX_U32 nPortIndex;OMX_U32 nUsage;} usage={0};
    _Static_assert(sizeof(usage)==16,"Android native usage ABI");
    usage.nSize=sizeof(usage);usage.nVersion=enable.nVersion;usage.nPortIndex=1;
    e=component->GetParameter(handle,indices[2],&usage);
    printf("NATIVE_USAGE result=0x%08x usage=0x%08x\n",(unsigned)e,usage.nUsage);
    if(e!=OMX_ErrorNone)return 13;
    OMX_PARAM_PORTDEFINITIONTYPE port={0};port.nSize=sizeof(port);port.nVersion=enable.nVersion;port.nPortIndex=1;
    e=component->GetParameter(handle,OMX_IndexParamPortDefinition,&port);
    printf("NATIVE_PORT result=0x%08x width=%u height=%u stride=%d scan=%u size=%u color=0x%x count=%u\n",
        (unsigned)e,port.format.video.nFrameWidth,port.format.video.nFrameHeight,
        port.format.video.nStride,port.format.video.nSliceHeight,port.nBufferSize,
        (unsigned)port.format.video.eColorFormat,port.nBufferCountActual);
    if(e!=OMX_ErrorNone)return 14;
#ifdef QUEST2_NATIVE_IMPORT
    return quest2_omx_native_import(handle,component,&port,usage.nUsage);
#else
    return 0;
#endif
}
static int usage(const char *program, int code) {
    FILE *out = code ? stderr : stdout;
    fprintf(out, "Usage: %s ABSOLUTE_LIBOMXCORE_PATH [--init | --enum | --get-handle COMPONENT | --native-buffers COMPONENT]\n"
      "Default: dlopen/dlsym only. Constructors can run. --init initializes core; --enum also enumerates.\n"
      "--get-handle can open hardware/trigger its driver initialization; opt-in only.\n"
      "--native-buffers additionally enables native output and queries usage/linear NV12; no buffers or frames.\nNo service registration, state change, frame decode, firmware replacement or system writes.\n", program);
#ifdef QUEST2_NATIVE_IMPORT
    fputs("This build also imports/releases one original native gralloc buffer with --native-buffers.\n",out);
#endif
    return code;
}
int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0); setvbuf(stderr, NULL, _IONBF, 0);
    if (argc == 2 && !strcmp(argv[1], "--help")) return usage(argv[0], 0);
    if (argc < 2 || argv[1][0] != '/') return usage(argv[0], 64);
    int mode = 0;
    if (argc > 2) {
        if (argc == 3 && !strcmp(argv[2], "--init")) mode = 1;
        else if (argc == 3 && !strcmp(argv[2], "--enum")) mode = 2;
        else if (argc == 4 && !strcmp(argv[2], "--get-handle") && argv[3][0]) mode = 3;
        else if (argc == 4 && !strcmp(argv[2], "--native-buffers") && argv[3][0]) mode = 4;
        else return usage(argv[0], 64);
    }
    printf("PROBE pointer_bits=%zu mode=%d service_registration=0 decode=0\n", 8 * sizeof(void *), mode);
    printf("DLOPEN_BEGIN path=%s\n", argv[1]);
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "DLOPEN_ERROR %s\n", dlerror()); return 2; }
    puts("DLOPEN_OK");
    __typeof__(&OMX_Init) init = symbol(library, "OMX_Init");
    __typeof__(&OMX_Deinit) deinit = symbol(library, "OMX_Deinit");
    __typeof__(&OMX_ComponentNameEnum) enumerate = symbol(library, "OMX_ComponentNameEnum");
    __typeof__(&OMX_GetHandle) get = symbol(library, "OMX_GetHandle");
    __typeof__(&OMX_FreeHandle) release = symbol(library, "OMX_FreeHandle");
    if (!init || !deinit || !enumerate || !get || !release) { dlclose(library); return 3; }
    maps();
    if (!mode) { dlclose(library); puts("RESULT load_only=ok hardware_handle_requested=0"); return 0; }
    puts("OMX_INIT_BEGIN");
    OMX_ERRORTYPE error = init(); printf("OMX_INIT=0x%08x\n", (unsigned)error);
    if (error != OMX_ErrorNone) { dlclose(library); return 4; }
    int result = 0;
    if (mode == 2) {
        for (OMX_U32 i = 0; ; ++i) {
            char name[OMX_MAX_STRINGNAME_SIZE] = {0};
            error = enumerate(name, sizeof(name), i);
            printf("OMX_ENUM index=%" PRIu32 " status=0x%08x name=%.*s\n", i,
                   (unsigned)error, (int)sizeof(name), name);
            if (error == OMX_ErrorNoMore) break;
            if (error != OMX_ErrorNone || i == UINT32_MAX) { result = 5; break; }
        }
    } else if (mode >= 3) {
        OMX_CALLBACKTYPE callbacks = { event, empty, fill };
        OMX_HANDLETYPE handle = NULL;
        printf("OMX_GET_HANDLE_BEGIN component=%s\n", argv[3]);
        error = get(&handle, argv[3], NULL, &callbacks);
        printf("OMX_GET_HANDLE=0x%08x handle=%p\n", (unsigned)error, handle);
        maps();
        if (error != OMX_ErrorNone || !handle) result = 6;
        if (error == OMX_ErrorNone && handle) {
            OMX_COMPONENTTYPE *component = handle;
            OMX_STATETYPE state = OMX_StateInvalid;
            if (component->GetState) {
                error = component->GetState(handle, &state);
                printf("OMX_GET_STATE=0x%08x state=%u (Loaded=%u)\n", (unsigned)error,
                       (unsigned)state, (unsigned)OMX_StateLoaded);
                if (error != OMX_ErrorNone) result = 7;
            } else { puts("OMX_GET_STATE_MISSING"); result = 7; }
            if(mode==4 && !result)result=native_parameters(handle,component);
            puts("OMX_FREE_HANDLE_BEGIN"); error = release(handle);
            printf("OMX_FREE_HANDLE=0x%08x\n", (unsigned)error);
            if (error != OMX_ErrorNone) {
                /* Failed component destruction: don't unload its callback code. */
                fflush(NULL); _Exit(8);
            }
        }
    }
    puts("OMX_DEINIT_BEGIN"); error = deinit();
    printf("OMX_DEINIT=0x%08x\n", (unsigned)error);
    if (error != OMX_ErrorNone) result = 9;
    dlclose(library);
    printf("RESULT exit=%d decode_attempted=0 service_registered=0\n", result);
    return result;
}