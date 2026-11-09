#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <OMX_Component.h>
static OMX_ERRORTYPE state(OMX_HANDLETYPE h, OMX_STATETYPE *s) { (void)h; *s=OMX_StateLoaded; return OMX_ErrorNone; }
static OMX_ERRORTYPE extension(OMX_HANDLETYPE h,OMX_STRING name,OMX_INDEXTYPE *index){
 (void)h;
 if(getenv("FIXTURE_NO_NATIVE"))return OMX_ErrorUnsupportedIndex;
 const char *names[]={"OMX.google.android.index.enableAndroidNativeBuffers","OMX.google.android.index.useAndroidNativeBuffer2","OMX.google.android.index.getAndroidNativeBufferUsage"};
 for(unsigned i=0;i<3;i++)if(!strcmp(name,names[i])){*index=(OMX_INDEXTYPE)(0x7f000001+i);return OMX_ErrorNone;}
 return OMX_ErrorUnsupportedIndex;
}
static OMX_ERRORTYPE set(OMX_HANDLETYPE h,OMX_INDEXTYPE index,OMX_PTR data){
 (void)h;
 if(index==(OMX_INDEXTYPE)0x7f000001){OMX_PARAM_U32TYPE *p=data;assert(p->nSize==16&&p->nPortIndex==1&&p->nU32==1);return OMX_ErrorNone;}
 assert(index==OMX_IndexParamVideoPortFormat);
 OMX_VIDEO_PARAM_PORTFORMATTYPE *p=data;
 assert(p->nSize==sizeof(*p)&&p->nPortIndex==1&&p->eColorFormat==0x7fa30c04);
 if(getenv("FIXTURE_REFUSE_LINEAR"))return OMX_ErrorUnsupportedSetting;
 return OMX_ErrorNone;
}
static OMX_ERRORTYPE parameter(OMX_HANDLETYPE h,OMX_INDEXTYPE index,OMX_PTR data){
 (void)h;
 if(index==(OMX_INDEXTYPE)0x7f000003){OMX_PARAM_U32TYPE *p=data;assert(p->nSize==16&&p->nPortIndex==1);p->nU32=0x20000000;return OMX_ErrorNone;}
 assert(index==OMX_IndexParamPortDefinition);
 OMX_PARAM_PORTDEFINITIONTYPE *p=data;assert(p->nSize==sizeof(*p)&&p->nPortIndex==1);
 p->format.video.nFrameWidth=320;p->format.video.nFrameHeight=256;p->format.video.nStride=384;p->format.video.nSliceHeight=256;
 p->format.video.eColorFormat=(OMX_COLOR_FORMATTYPE)0x7fa30c04;p->nBufferSize=159744;p->nBufferCountActual=18;
 return OMX_ErrorNone;
}
static OMX_COMPONENTTYPE component = {.GetState=state,.GetExtensionIndex=extension,.SetParameter=set,.GetParameter=parameter};

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