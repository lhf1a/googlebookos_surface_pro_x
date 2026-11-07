/* Explicit host mock: generates synthetic pixels, NEVER a vendor decoder.
 * Exercises the client lifecycle, ownership, fragmentation and row extraction.
 */
#include <OMX_Component.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define INIT(x) do { memset(&(x),0,sizeof(x)); (x).nSize=sizeof(x); (x).nVersion.s.nVersionMajor=1; (x).nVersion.s.nVersionMinor=1; } while(0)
struct record { OMX_BUFFERHEADERTYPE *b; OMX_U32 port; int queued; };
static struct {
    OMX_COMPONENTTYPE component;
    OMX_CALLBACKTYPE callbacks;
    OMX_PTR app;
    OMX_STATETYPE state;
    OMX_PARAM_PORTDEFINITIONTYPE ports[2];
    struct record buffers[32];
    int count, pending, pending_arg, reconfigured, failed_etb, eos;
    OMX_TICKS frames[32];
    int frame_count, frame_next;
    pthread_t event_thread;
    int thread_active;
} mock;
static int enabled(const char *name) { return getenv(name) != NULL; }
static OMX_ERRORTYPE report(OMX_EVENTTYPE event, OMX_U32 a, OMX_U32 b) {
    return mock.callbacks.EventHandler(&mock.component,mock.app,event,a,b,NULL);
}
struct completion { int command, argument; };
static void *complete_later(void *opaque) {
    struct completion *c=opaque;
    usleep(1000);
    report(OMX_EventCmdComplete,c->command,c->argument);
    free(c);return NULL;
}
static int allocated(int port) {
    int count=0; for(int i=0;i<32;++i) count+=mock.buffers[i].b && (int)mock.buffers[i].port==port;
    return count;
}
static void completed(void) {
    int command=mock.pending, argument=mock.pending_arg;
    if(command<0)return;
    if(command==OMX_CommandStateSet && argument==OMX_StateIdle && mock.state==OMX_StateLoaded) {
        if(allocated(0)!=(int)mock.ports[0].nBufferCountActual || allocated(1)!=(int)mock.ports[1].nBufferCountActual)return;
    } else if(command==OMX_CommandStateSet && argument==OMX_StateLoaded) {
        if(allocated(0)||allocated(1))return;
    } else if(command==OMX_CommandPortDisable && allocated(argument))return;
    else if(command==OMX_CommandPortEnable && allocated(argument)!=(int)mock.ports[argument].nBufferCountActual)return;
    mock.pending=-1;
    if(command==OMX_CommandStateSet)mock.state=argument;
    if(enabled("MOCK_ASYNC")) {
        if(mock.thread_active)pthread_join(mock.event_thread,NULL);
        struct completion *c=malloc(sizeof(*c));if(!c)abort();
        *c=(struct completion){command,argument};
        if(pthread_create(&mock.event_thread,NULL,complete_later,c))abort();
        mock.thread_active=1;
    } else report(OMX_EventCmdComplete,command,argument);
}
static void return_outputs(void) {
    for(int i=0;i<32;++i)if(mock.buffers[i].b && mock.buffers[i].port==1 && mock.buffers[i].queued) {
        OMX_BUFFERHEADERTYPE *b=mock.buffers[i].b;mock.buffers[i].queued=0;
        b->nFilledLen=b->nFlags=0;mock.callbacks.FillBufferDone(&mock.component,mock.app,b);
    }
}
static OMX_ERRORTYPE command(OMX_HANDLETYPE h,OMX_COMMANDTYPE cmd,OMX_U32 argument,OMX_PTR data) {
    (void)h;(void)data;printf("MOCK_COMMAND %u %u\n",cmd,argument);
    if(mock.pending>=0)return OMX_ErrorIncorrectStateOperation;
    mock.pending=cmd;mock.pending_arg=argument;
    if((cmd==OMX_CommandStateSet && argument==OMX_StateIdle && mock.state==OMX_StateExecuting) || cmd==OMX_CommandPortDisable)return_outputs();
    completed();return OMX_ErrorNone;
}
static OMX_ERRORTYPE get_parameter(OMX_HANDLETYPE h,OMX_INDEXTYPE index,OMX_PTR data) {
    (void)h;
    if(index==OMX_IndexParamVideoInit) {
        OMX_PORT_PARAM_TYPE *p=data;p->nPorts=2;p->nStartPortNumber=0;return OMX_ErrorNone;
    }
    if(index==OMX_IndexParamPortDefinition) {
        OMX_PARAM_PORTDEFINITIONTYPE *p=data;if(p->nPortIndex>1)return OMX_ErrorBadPortIndex;
        *p=mock.ports[p->nPortIndex];return OMX_ErrorNone;
    }
    return OMX_ErrorUnsupportedIndex;
}
static OMX_ERRORTYPE set_parameter(OMX_HANDLETYPE h,OMX_INDEXTYPE index,OMX_PTR data) {
    (void)h;(void)data;
    if(index==OMX_IndexParamPortDefinition)return OMX_ErrorNone;
    if(index==OMX_IndexParamVideoPortFormat)return enabled("MOCK_FORMAT_FAIL")?OMX_ErrorUnsupportedSetting:OMX_ErrorNone;
    return OMX_ErrorUnsupportedIndex;
}
static OMX_ERRORTYPE get_config(OMX_HANDLETYPE h,OMX_INDEXTYPE index,OMX_PTR data) {
    (void)h;
    if(index!=OMX_IndexConfigCommonOutputCrop)return OMX_ErrorUnsupportedIndex;
    OMX_CONFIG_RECTTYPE *crop=data;crop->nLeft=crop->nTop=0;crop->nWidth=crop->nHeight=4;return OMX_ErrorNone;
}
static OMX_ERRORTYPE state(OMX_HANDLETYPE h,OMX_STATETYPE *value) { (void)h;*value=mock.state;return OMX_ErrorNone; }
static OMX_ERRORTYPE allocate(OMX_HANDLETYPE h,OMX_BUFFERHEADERTYPE **out,OMX_U32 port,OMX_PTR app,OMX_U32 size) {
    (void)h;
    if(enabled("MOCK_ALLOC_FAIL") && port==1 && allocated(1)==1)return OMX_ErrorInsufficientResources;
    for(int i=0;i<32;++i)if(!mock.buffers[i].b) {
        OMX_BUFFERHEADERTYPE *b=calloc(1,sizeof(*b));if(!b)return OMX_ErrorInsufficientResources;
        b->pBuffer=calloc(1,size);if(!b->pBuffer){free(b);return OMX_ErrorInsufficientResources;}
        b->nAllocLen=size;b->pAppPrivate=app;mock.buffers[i]=(struct record){b,port,0};
        ++mock.count;*out=b;completed();return OMX_ErrorNone;
    }
    return OMX_ErrorInsufficientResources;
}
static OMX_ERRORTYPE free_buffer(OMX_HANDLETYPE h,OMX_U32 port,OMX_BUFFERHEADERTYPE *b) {
    (void)h;(void)port;
    for(int i=0;i<32;++i)if(mock.buffers[i].b==b) {
        if(mock.buffers[i].queued)return OMX_ErrorIncorrectStateOperation;
        free(b->pBuffer);free(b);mock.buffers[i].b=NULL;--mock.count;completed();return OMX_ErrorNone;
    }
    return OMX_ErrorBadParameter;
}
static void drain(void) {
    if(!mock.eos)return;
    for(int i=0;i<32;++i)if(mock.buffers[i].b && mock.buffers[i].queued) {
        OMX_BUFFERHEADERTYPE *b=mock.buffers[i].b;mock.buffers[i].queued=0;b->nOffset=0;
        if(mock.frame_next<mock.frame_count) {
            unsigned stride=mock.ports[1].format.video.nStride,slice=mock.ports[1].format.video.nSliceHeight;
            memset(b->pBuffer,0xee,b->nAllocLen);
            unsigned char value=(unsigned char)(0x10+mock.frame_next);
            for(unsigned y=0;y<4;++y)memset(b->pBuffer+y*stride,value,4);
            for(unsigned y=0;y<2;++y)memset(b->pBuffer+stride*slice+y*stride,value+0x40,4);
            b->nFilledLen=stride*slice*3/2;b->nFlags=OMX_BUFFERFLAG_ENDOFFRAME;
            b->nTimeStamp=mock.frames[mock.frame_next++];
            if(enabled("MOCK_BAD_BOUNDS"))b->nFilledLen=b->nAllocLen+1;
        } else {b->nFilledLen=0;b->nFlags=OMX_BUFFERFLAG_EOS;mock.eos=0;}
        mock.callbacks.FillBufferDone(&mock.component,mock.app,b);
        if(!mock.eos)break;
    }
}
static OMX_ERRORTYPE fill(OMX_HANDLETYPE h,OMX_BUFFERHEADERTYPE *b) {
    (void)h;
    for(int i=0;i<32;++i)if(mock.buffers[i].b==b){mock.buffers[i].queued=1;drain();return OMX_ErrorNone;}
    return OMX_ErrorBadParameter;
}
static int compare_pts(const void *a,const void *b){OMX_TICKS x=*(const OMX_TICKS*)a,y=*(const OMX_TICKS*)b;return (x>y)-(x<y);}
static OMX_ERRORTYPE empty(OMX_HANDLETYPE h,OMX_BUFFERHEADERTYPE *b) {
    (void)h;printf("MOCK_ETB bytes=%u flags=0x%x pts=%lld\n",b->nFilledLen,b->nFlags,(long long)b->nTimeStamp);
    if(enabled("MOCK_ETB_FAIL") && !mock.failed_etb++){return OMX_ErrorHardware;}
    if(b->nFlags&OMX_BUFFERFLAG_ENDOFFRAME){
        if(mock.frame_count==32)return OMX_ErrorInsufficientResources;
        mock.frames[mock.frame_count++]=b->nTimeStamp;
        if(enabled("MOCK_RECONFIGURE") && !mock.reconfigured++) {
            mock.ports[1].format.video.nStride=12;mock.ports[1].format.video.nSliceHeight=8;mock.ports[1].nBufferSize=144;
            report(OMX_EventPortSettingsChanged,1,OMX_IndexParamPortDefinition);
        }
    }
    if(b->nFlags&OMX_BUFFERFLAG_EOS){mock.eos=1;qsort(mock.frames,mock.frame_count,sizeof(mock.frames[0]),compare_pts);}
    mock.callbacks.EmptyBufferDone(&mock.component,mock.app,b);drain();return OMX_ErrorNone;
}
OMX_ERRORTYPE OMX_Init(void){puts("MOCK_INIT");return OMX_ErrorNone;}
OMX_ERRORTYPE OMX_Deinit(void){puts("MOCK_DEINIT");return OMX_ErrorNone;}
OMX_ERRORTYPE OMX_GetHandle(OMX_HANDLETYPE *handle,OMX_STRING name,OMX_PTR app,OMX_CALLBACKTYPE *callbacks){
    (void)name;memset(&mock,0,sizeof(mock));mock.pending=-1;mock.state=OMX_StateLoaded;mock.app=app;mock.callbacks=*callbacks;
    mock.component.SendCommand=command;mock.component.GetParameter=get_parameter;mock.component.SetParameter=set_parameter;
    mock.component.GetConfig=get_config;mock.component.GetState=state;mock.component.AllocateBuffer=allocate;
    mock.component.FreeBuffer=free_buffer;mock.component.FillThisBuffer=fill;mock.component.EmptyThisBuffer=empty;
    for(int i=0;i<2;++i){
        INIT(mock.ports[i]);mock.ports[i].nPortIndex=i;mock.ports[i].eDir=i?OMX_DirOutput:OMX_DirInput;
        mock.ports[i].eDomain=OMX_PortDomainVideo;mock.ports[i].nBufferCountActual=i?3:2;mock.ports[i].nBufferCountMin=mock.ports[i].nBufferCountActual;
        mock.ports[i].bEnabled=OMX_TRUE;mock.ports[i].nBufferSize=i?72:4;
        mock.ports[i].format.video.nFrameWidth=mock.ports[i].format.video.nFrameHeight=4;
        mock.ports[i].format.video.nStride=8;mock.ports[i].format.video.nSliceHeight=6;
        mock.ports[i].format.video.eCompressionFormat=i?OMX_VIDEO_CodingUnused:OMX_VIDEO_CodingAVC;
        mock.ports[i].format.video.eColorFormat=i?OMX_COLOR_FormatYUV420SemiPlanar:OMX_COLOR_FormatUnused;
    }
    if(enabled("MOCK_NONLINEAR"))mock.ports[1].format.video.eColorFormat=(OMX_COLOR_FORMATTYPE)0x7fa30c06;
    *handle=&mock.component;return OMX_ErrorNone;
}
OMX_ERRORTYPE OMX_FreeHandle(OMX_HANDLETYPE handle){
    (void)handle;printf("MOCK_FREE_HANDLE buffers_remaining=%d state=%u\n",mock.count,mock.state);
    if(mock.thread_active)pthread_join(mock.event_thread,NULL);
    for(int i=0;i<32;++i)if(mock.buffers[i].b){free(mock.buffers[i].b->pBuffer);free(mock.buffers[i].b);mock.buffers[i].b=NULL;}
    mock.count=0;return OMX_ErrorNone;
}
