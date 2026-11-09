/* Private OMX diagnostic; native pBuffer is never dereferenced as pixels. */
#ifndef QUEST2_OMX_NATIVE_OUTPUT_H
#define QUEST2_OMX_NATIVE_OUTPUT_H
#include <linux/dma-buf.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
extern int quest2_capture_codec_usage(uint32_t);
extern void quest2_capture_source_size(unsigned *,unsigned *);
extern void quest2_capture_image_size(unsigned *,unsigned *);
extern int quest2_capture_config(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned);
extern int quest2_capture_allocate(unsigned,int *,size_t *);
extern int quest2_capture_layout(unsigned *,unsigned *,unsigned *);
extern const void *quest2_capture_handle(unsigned);
extern int quest2_capture_sample(unsigned,const uint8_t *,size_t);
extern void quest2_capture_finish(void);
static bool q2_omx_native_enable(OMX_HANDLETYPE h,OMX_COMPONENTTYPE *c,unsigned port,uint32_t *usage) {
    const char *names[]={"OMX.google.android.index.enableAndroidNativeBuffers",
        "OMX.google.android.index.useAndroidNativeBuffer2","OMX.google.android.index.getAndroidNativeBufferUsage"};
    OMX_INDEXTYPE indices[3];
    if(!c->GetExtensionIndex || !c->UseBuffer)return false;
    for(unsigned i=0;i<3;i++)
        if(c->GetExtensionIndex(h,(OMX_STRING)names[i],&indices[i])!=OMX_ErrorNone)return false;
    struct {OMX_U32 nSize;OMX_VERSIONTYPE nVersion;OMX_U32 nPortIndex,value;} param={0};
    _Static_assert(sizeof(param)==16,"Native buffer parameter ABI");
    param.nSize=sizeof(param);param.nVersion.nVersion=0x00000101;param.nPortIndex=port;param.value=OMX_TRUE;
    if(c->SetParameter(h,indices[0],&param)!=OMX_ErrorNone)return false;
    param.value=0;
    if(c->GetParameter(h,indices[2],&param)!=OMX_ErrorNone || param.value!=0x20000000)return false;
    const char *enabled=getenv("QUEST2_VIDC_NATIVE_LAYOUT");
    if(!enabled || strcmp(enabled,"1"))return false;
    *usage=param.value;printf("NATIVE_OUTPUT usage=0x%x\n",*usage);return true;
}
static bool q2_omx_native_config(const OMX_PARAM_PORTDEFINITIONTYPE *p,const OMX_CONFIG_RECTTYPE *crop,uint32_t usage) {
    if(p->nBufferCountActual>32 || p->format.video.eColorFormat!=0x7fa30c04 ||
       crop->nLeft || crop->nTop || !crop->nWidth || !crop->nHeight)return false;
    if(quest2_capture_codec_usage(usage))return false;
    /* OEM GetConfig crop can lag until the first new frame. The paired bridge
     * has already obtained the new crop from the real kernel SOURCE_CHANGE. */
    unsigned width=crop->nWidth,height=crop->nHeight;
    quest2_capture_source_size(&width,&height);
    return !quest2_capture_config(width,height,width,height,p->nBufferSize,1);
}
static bool q2_omx_native_allocate(OMX_HANDLETYPE h,OMX_COMPONENTTYPE *c,
    const OMX_PARAM_PORTDEFINITIONTYPE *p,unsigned index,void *app,OMX_BUFFERHEADERTYPE **header) {
    int fd=-1;size_t length=0;
    if(quest2_capture_allocate(index,&fd,&length))return false;
    close(fd);
    if(length>UINT32_MAX || length<p->nBufferSize)return false;
    OMX_ERRORTYPE e=c->UseBuffer(h,header,p->nPortIndex,app,(OMX_U32)length,(OMX_U8 *)quest2_capture_handle(index));
    printf("NATIVE_USE_BUFFER index=%u result=0x%x bytes=%zu\n",index,(unsigned)e,length);
    return e==OMX_ErrorNone && *header && (*header)->pAppPrivate==app &&
        (*header)->pBuffer==quest2_capture_handle(index);
}
struct q2_omx_native_read {
    int fd;void *map;size_t length;unsigned stride,scan,width,height;
    OMX_BUFFERHEADERTYPE header;
};
static bool q2_omx_native_read_end(struct q2_omx_native_read *r) {
    struct dma_buf_sync sync={.flags=DMA_BUF_SYNC_END|DMA_BUF_SYNC_READ};
    int result=ioctl(r->fd,DMA_BUF_IOCTL_SYNC,&sync);
    int unmapped=munmap(r->map,r->length),closed=close(r->fd);
    return !result && !unmapped && !closed;
}
static bool q2_omx_native_read(unsigned index,const OMX_BUFFERHEADERTYPE *header,struct q2_omx_native_read *r) {
    unsigned size=0;
    if(header->nOffset || header->pBuffer!=quest2_capture_handle(index) ||
       quest2_capture_layout(&r->stride,&r->scan,&size) ||
       quest2_capture_allocate(index,&r->fd,&r->length))return false;
    if(header->nFilledLen>header->nAllocLen || header->nAllocLen>r->length ||
       header->nFilledLen<(uint64_t)r->stride*r->scan*3/2 ||
       (header->nFlags&OMX_BUFFERFLAG_DATACORRUPT)){close(r->fd);return false;}
    r->map=mmap(NULL,r->length,PROT_READ,MAP_SHARED,r->fd,0);
    if(r->map==MAP_FAILED){close(r->fd);return false;}
    struct dma_buf_sync sync={.flags=DMA_BUF_SYNC_START|DMA_BUF_SYNC_READ};
    if(ioctl(r->fd,DMA_BUF_IOCTL_SYNC,&sync)){munmap(r->map,r->length);close(r->fd);return false;}
    if(quest2_capture_sample(index,r->map,0)){q2_omx_native_read_end(r);return false;}
    quest2_capture_image_size(&r->width,&r->height);
    r->header=*header;r->header.pBuffer=r->map;
    return true;
}
#endif
