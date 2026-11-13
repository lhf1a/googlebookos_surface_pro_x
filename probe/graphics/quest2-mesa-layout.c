/* SPDX-License-Identifier: MIT
 * Hybrid-only QTI allocator/Mesa compatibility, pinned vendor utility ABI.
 * Freedreno linear imports require four padded rows. Padding is applied BEFORE
 * the donor computes/allocates size, not to an already allocated native handle.
 * Original logical dimensions remain in the gralloc descriptor and handle.
 * The renamed vendor dependency supplies every other utility export unchanged.
 * UBWC, YUV, multilayer, unknown formats retain donor behavior.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <pthread.h>
#include <limits.h>
struct BufferInfo { int width,height,format,layer_count;uint64_t usage; };
static void *vendor;
static void (*original)(const struct BufferInfo *,unsigned *,unsigned *);
static bool (*is_ubwc)(int,uint64_t);
static pthread_once_t once=PTHREAD_ONCE_INIT;
static void init(void) {
 vendor=dlopen("libsq2_grallocutils_vendor.so",RTLD_NOW|RTLD_LOCAL);
 if(!vendor)abort();
 original=dlsym(vendor,"_ZN7gralloc24GetAlignedWidthAndHeightERKNS_10BufferInfoEPjS3_");
 is_ubwc=dlsym(vendor,"_ZN7gralloc13IsUBwcEnabledEim");
 if(!original || !is_ubwc)abort();
}
void aligned(const struct BufferInfo *info,unsigned *w,unsigned *h)
 __asm__("_ZN7gralloc24GetAlignedWidthAndHeightERKNS_10BufferInfoEPjS3_");
void aligned(const struct BufferInfo *info,unsigned *w,unsigned *h) {
 pthread_once(&once,init);
 original(info,w,h);
 if(!info || !w || !h || info->width<=0 || info->height<=0 || info->layer_count!=1)return;
 unsigned cpp=0;
 switch(info->format) {
 case 1:case 2:case 5:case 43:cpp=4;break;
 case 22:cpp=8;break;
 default:return;
 }
 if(is_ubwc(info->format,info->usage))return;
 unsigned pixels=64/cpp;
 /* Real allocation and all metadata users share this aligned geometry.
  * Logical width/height are unchanged; never shrink vendor requirements. */
 if(*w>INT_MAX-(pixels-1) || *h>INT_MAX-3) { *w=*h=0;return; }
 *w=(*w+pixels-1)&~(pixels-1);
 *h=(*h+3)&~3u;
}
static struct BufferInfo allocation_info(const struct BufferInfo *in) {
 struct BufferInfo v=*in;
 pthread_once(&once,init);
 if(v.width>0 && v.height>0 && v.layer_count==1 &&
    (v.format==1 || v.format==2 || v.format==5 || v.format==43 || v.format==22) &&
    !is_ubwc(v.format,v.usage)) {
  if(v.height>INT_MAX-3) {v.height=0;return v;}
  v.height=(v.height+3)&~3;
 }
 return v;
}
int size4(const struct BufferInfo *in,unsigned *s,unsigned *w,unsigned *h)
 __asm__("_ZN7gralloc26GetBufferSizeAndDimensionsERKNS_10BufferInfoEPjS3_S3_");
int size4(const struct BufferInfo *in,unsigned *s,unsigned *w,unsigned *h) {
 struct BufferInfo v=allocation_info(in);
 int (*f)(const struct BufferInfo *,unsigned*,unsigned*,unsigned*)=
  dlsym(vendor,"_ZN7gralloc26GetBufferSizeAndDimensionsERKNS_10BufferInfoEPjS3_S3_");
 if(!f || !v.height)return -22;
 return f(&v,s,w,h);
}
int size5(const struct BufferInfo *in,unsigned *s,unsigned *w,unsigned *h,void *meta)
 __asm__("_ZN7gralloc26GetBufferSizeAndDimensionsERKNS_10BufferInfoEPjS3_S3_P16GraphicsMetadata");
int size5(const struct BufferInfo *in,unsigned *s,unsigned *w,unsigned *h,void *meta) {
 struct BufferInfo v=allocation_info(in);
 int (*f)(const struct BufferInfo *,unsigned*,unsigned*,unsigned*,void*)=
  dlsym(vendor,"_ZN7gralloc26GetBufferSizeAndDimensionsERKNS_10BufferInfoEPjS3_S3_P16GraphicsMetadata");
 if(!f || !v.height)return -22;
 return f(&v,s,w,h,meta);
}
