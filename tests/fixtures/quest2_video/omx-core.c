#include <stdio.h>
#include <string.h>
#include <OMX_Component.h>
static OMX_ERRORTYPE state(OMX_HANDLETYPE h, OMX_STATETYPE *s) { (void)h; *s=OMX_StateLoaded; return OMX_ErrorNone; }
static OMX_COMPONENTTYPE component = {.GetState=state};
OMX_ERRORTYPE OMX_Init(void) { puts("FIXTURE_INIT"); return OMX_ErrorNone; }
OMX_ERRORTYPE OMX_Deinit(void) { puts("FIXTURE_DEINIT"); return OMX_ErrorNone; }
OMX_ERRORTYPE OMX_ComponentNameEnum(OMX_STRING name, OMX_U32 size, OMX_U32 index) {
    if (index > 0) return OMX_ErrorNoMore;
    if (size < sizeof("OMX.fixture.decoder.avc")) return OMX_ErrorBadParameter;
    strcpy(name,"OMX.fixture.decoder.avc"); return OMX_ErrorNone;
}
OMX_ERRORTYPE OMX_GetHandle(OMX_HANDLETYPE *h, OMX_STRING name, OMX_PTR data, OMX_CALLBACKTYPE *cb) {
    (void)data; (void)cb; puts("FIXTURE_GET_HANDLE");
    if (strcmp(name, "OMX.fixture.decoder.avc")) return OMX_ErrorComponentNotFound;
    *h=&component; return OMX_ErrorNone;
}
OMX_ERRORTYPE OMX_FreeHandle(OMX_HANDLETYPE h) { (void)h; puts("FIXTURE_FREE_HANDLE"); return OMX_ErrorNone; }