#pragma once
#include <OMX_Component.h>
#include <stdint.h>
#include <string.h>
struct quest2_port32 {
 uint32_t size,version,port,direction,actual,minimum,bytes,enabled,populated,domain;
 struct {uint32_t mime,render,width,height;int32_t stride;
  uint32_t slice,bitrate,framerate,conceal,compression,color,window;} video;
 uint32_t contiguous,alignment;
};
/* Only numeric video parameters cross processes; opaque pointers never do. */
static inline int quest2_port_to_native(const struct quest2_port32 *p, OMX_PARAM_PORTDEFINITIONTYPE *n, int writing) {
 if(p->size!=sizeof(*p)||p->video.mime||p->video.render||p->video.window)return 0;
 if(writing&&p->domain!=OMX_PortDomainVideo)return 0;
 memset(n,0,sizeof(*n));memcpy(n,p,40);n->nSize=sizeof(*n);
 memcpy(&n->format.video.nFrameWidth,&p->video.width,36);
 n->bBuffersContiguous=(OMX_BOOL)p->contiguous;n->nBufferAlignment=p->alignment;
 return 1;
}
static inline int quest2_port_to_wire(const OMX_PARAM_PORTDEFINITIONTYPE *n,struct quest2_port32 *p) {
 if(n->nSize!=sizeof(*n)||n->eDomain!=OMX_PortDomainVideo)return 0;
 memset(p,0,sizeof(*p));memcpy(p,n,40);p->size=sizeof(*p);
 memcpy(&p->video.width,&n->format.video.nFrameWidth,36);
 p->contiguous=n->bBuffersContiguous;p->alignment=n->nBufferAlignment;return 1;
}

/* Report the allocated raster; crop remains the visible size. NV12's AOSP
 * renderer steps by width; P010 uses byte stride with two bytes per sample. */
#define QUEST2_COLOR_P010 54U
#define QUEST2_COLOR_NV12_VENUS 0x7fa30c04U
#define QUEST2_COLOR_P010_VENUS 0x7fa30c0aU
static inline int quest2_port_linear_layout(struct quest2_port32 *p) {
 if(p->port!=1 || (p->video.color!=QUEST2_COLOR_NV12_VENUS &&
                  p->video.color!=QUEST2_COLOR_P010_VENUS))return 1;
 unsigned sample=p->video.color==QUEST2_COLOR_P010_VENUS?2:1;
 if(p->video.stride<=0 || (uint64_t)p->video.stride<(uint64_t)p->video.width*sample ||
    p->video.slice<p->video.height || !p->video.width || !p->video.height ||
    ((uint32_t)p->video.stride%(2*sample)) || (p->video.slice&1) ||
    (uint64_t)p->video.stride*p->video.slice*3/2>p->bytes)return 0;
 p->video.width=(uint32_t)p->video.stride/sample;
 p->video.height=p->video.slice;
 p->video.color=sample==2?QUEST2_COLOR_P010:OMX_COLOR_FormatYUV420SemiPlanar;
 return 1;
}
