#define _GNU_SOURCE
#include <OMX_Component.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/dma-buf.h>
#include <stdarg.h>
static int pixel,starts,ends,sampled,fail_sync;
static void *handle;
static int sync_stub(int fd,unsigned long request,...) {
    assert(fd>=0 && request==DMA_BUF_IOCTL_SYNC);
    va_list args;va_start(args,request);struct dma_buf_sync *s=va_arg(args,struct dma_buf_sync *);va_end(args);
    assert(s->flags&DMA_BUF_SYNC_READ);assert(!(s->flags&DMA_BUF_SYNC_WRITE));
    if(fail_sync)return -1;
    if(s->flags&DMA_BUF_SYNC_END)ends++;else starts++;return 0;
}
#define ioctl sync_stub
#include "../../../probe/video/quest2-omx-native-output.h"
#undef ioctl
void quest2_capture_source_size(unsigned *w,unsigned *h){*w=4;*h=4;}
void quest2_capture_image_size(unsigned *w,unsigned *h){*w=4;*h=4;}
int quest2_capture_codec_usage(uint32_t usage){return usage==0x20000000?0:-1;}
int quest2_capture_config(unsigned w,unsigned h,unsigned stride,unsigned scan,unsigned size,unsigned bytes) {
    assert(w==4 && h==4 && stride==4 && scan==4 && size==96 && bytes==1);return 0;
}
int quest2_capture_allocate(unsigned index,int *fd,size_t *length) {
    assert(index==0);*fd=dup(pixel);*length=96;return *fd<0?-1:0;
}
int quest2_capture_layout(unsigned *stride,unsigned *scan,unsigned *size){*stride=8;*scan=8;*size=96;return 0;}
const void *quest2_capture_handle(unsigned index){assert(index==0);return handle;}
int quest2_capture_sample(unsigned index,const uint8_t *p,size_t offset) {
    assert(!index && !offset && p!=handle && p[0]==0x34 && p[95]==0x34);sampled++;return 0;
}
void quest2_capture_finish(void){}
static OMX_ERRORTYPE extension(OMX_HANDLETYPE h,OMX_STRING name,OMX_INDEXTYPE *index) {
    (void)h;*index=strstr(name,"enable")?1:strstr(name,"Usage")?3:2;return OMX_ErrorNone;
}
static OMX_ERRORTYPE parameter(OMX_HANDLETYPE h,OMX_INDEXTYPE index,OMX_PTR data) {
    (void)h;uint32_t *p=data;assert(p[0]==16 && p[2]==1);
    if(index==3)p[3]=0x20000000;else assert(index==1 && p[3]==1);return OMX_ErrorNone;
}
static OMX_ERRORTYPE use(OMX_HANDLETYPE h,OMX_BUFFERHEADERTYPE **out,OMX_U32 port,OMX_PTR app,OMX_U32 size,OMX_U8 *p) {
    (void)h;assert(port==1 && size==96 && p==handle);
    static OMX_BUFFERHEADERTYPE header;header=(OMX_BUFFERHEADERTYPE){.pAppPrivate=app,.pBuffer=p,.nAllocLen=size};
    *out=&header;return OMX_ErrorNone;
}
int main(void) {
    pixel=memfd_create("synthetic-pixel-data",MFD_CLOEXEC);assert(pixel>=0);
    uint8_t bytes[96];memset(bytes,0x34,sizeof(bytes));assert(write(pixel,bytes,96)==96);
    handle=mmap(NULL,4096,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(handle!=MAP_FAILED);
    OMX_COMPONENTTYPE component={.GetExtensionIndex=extension,.GetParameter=parameter,.SetParameter=parameter,.UseBuffer=use};
    setenv("QUEST2_VIDC_NATIVE_LAYOUT","1",1);
    uint32_t usage=0;assert(q2_omx_native_enable(NULL,&component,1,&usage) && usage==0x20000000);
    OMX_PARAM_PORTDEFINITIONTYPE port={.nPortIndex=1,.nBufferCountActual=2,.nBufferSize=96};
    port.format.video.eColorFormat=0x7fa30c04;
    OMX_CONFIG_RECTTYPE crop={.nWidth=2,.nHeight=2}; /* Stale OMX crop must not choose the allocation. */
    assert(q2_omx_native_config(&port,&crop,usage));
    OMX_BUFFERHEADERTYPE *header=NULL;int app=0;
    assert(q2_omx_native_allocate(NULL,&component,&port,0,&app,&header));
    header->nFilledLen=96;
    struct q2_omx_native_read r={0};
    assert(q2_omx_native_read(0,header,&r) && r.header.pBuffer!=header->pBuffer);
    assert(header->pBuffer==handle && !memcmp(r.header.pBuffer,bytes,96));
    assert(q2_omx_native_read_end(&r) && starts==1 && ends==1 && sampled==1);
    header->nFilledLen=97;assert(!q2_omx_native_read(0,header,&r));
    header->nFilledLen=95;assert(!q2_omx_native_read(0,header,&r));
    header->nFilledLen=96;header->nOffset=1;assert(!q2_omx_native_read(0,header,&r));
    header->nOffset=0;header->nFlags=OMX_BUFFERFLAG_DATACORRUPT;assert(!q2_omx_native_read(0,header,&r));
    header->nFlags=0;fail_sync=1;assert(!q2_omx_native_read(0,header,&r));
    assert(sampled==1 && starts==1 && ends==1);
    assert(!munmap(handle,4096));assert(!close(pixel));
    puts("NATIVE_OUTPUT: opaque handle untouched; read-only mapping and sync; invalid frames rejected before sampling");
}
