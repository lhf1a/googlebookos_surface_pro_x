/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
typedef uint8_t u8;typedef uint32_t u32;typedef uint64_t u64;
#define DRM_FORMAT_MAX_PLANES 4
#define DIV_ROUND_UP(n,d) (((n)+(d)-1)/(d))
#define DIV_ROUND_UP_ULL(n,d) (((n)+(d)-1)/(d))
#define ARRAY_SIZE(x) (sizeof(x)/sizeof(*(x)))
#define ERR_PTR(x) ((void*)(intptr_t)(x))
#define PTR_ERR(x) ((int)(intptr_t)(x))
#define IS_ERR(x) ((uintptr_t)(x)>=(uintptr_t)-4095)
#define GFP_KERNEL 0
#define DRM_MODE_FB_MODIFIERS 2
#define DBG(...) ((void)0)
#define dev_err(...) ((void)0)
#include "actual-format.h"
struct drm_device {void *dev_private,*dev;};struct drm_file {int unused;};
struct drm_gem_object {u64 size;int refs;};
struct drm_mode_fb_cmd2 {u32 width,height,pixel_format,flags;u32 handles[4],pitches[4],offsets[4];u64 modifier[4];};
struct drm_framebuffer {struct drm_device *dev;const struct drm_format_info *format;u32 width,height,pitches[4],offsets[4],flags;u64 modifier;struct drm_gem_object *obj[4];struct {int id;}base;};
struct msm_format {int id;};struct msm_kms;
struct msm_kms_funcs {const struct msm_format *(*get_format)(struct msm_kms*,u32,u64);int (*check_modified_format)(struct msm_kms*,const struct msm_format*,const struct drm_mode_fb_cmd2*,struct drm_gem_object**);};
struct msm_kms {const struct msm_kms_funcs *funcs;};struct msm_drm_private {struct msm_kms *kms;};
static const int msm_framebuffer_funcs=0;
static int checks,live,alloc_fail,format_fail,lookup_fail,lookups,put_count,init_fail,modified_fail,modified_calls,format_calls,info_calls;
static u32 seen_fourcc;static u64 seen_modifier;static const struct drm_format_info *resolved_info;
static struct drm_gem_object objects[4];static struct msm_format vendor_format={7};
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void *kzalloc(size_t n,int flags){if(alloc_fail)return NULL;void *p=calloc(1,n);if(p)live++;return p;}
static void kfree(void *p){if(p){live--;free(p);}}
static struct drm_gem_object *drm_gem_object_lookup(struct drm_file *file,u32 handle){lookups++;if(lookup_fail==lookups||handle<1||handle>4)return NULL;objects[handle-1].refs++;return &objects[handle-1];}
static void drm_gem_object_put(struct drm_gem_object *o){if(o){CHECK(o->refs>0);o->refs--;put_count++;}}
static const struct drm_format_info *drm_get_format_info(struct drm_device *dev,u32 format,u64 modifier){info_calls++;seen_fourcc=format;seen_modifier=modifier;return resolved_info;}
static const struct drm_format_info *drm_format_info(u32 format){return resolved_info;}
static const struct msm_format *get_format(struct msm_kms *kms,u32 format,u64 modifier){format_calls++;return format_fail?NULL:&vendor_format;}
static int check_modified(struct msm_kms *kms,const struct msm_format *f,const struct drm_mode_fb_cmd2 *cmd,struct drm_gem_object **bos){modified_calls++;CHECK(f==&vendor_format);for(int i=0;i<2;i++)CHECK(bos[i]&&bos[i]->refs>0);return modified_fail;}
static int drm_framebuffer_init(struct drm_device *dev,struct drm_framebuffer *fb,const int *funcs){return init_fail;}
#include "actual-fill.h"
#include "actual-source.h"
#include "actual-callback.h"
static const struct actual_mode_config callbacks={.fb_create=msm_framebuffer_create};
static void reset(void){CHECK(live==0);for(int i=0;i<4;i++){CHECK(objects[i].refs==0);objects[i].size=1ULL<<40;}alloc_fail=format_fail=lookup_fail=lookups=put_count=init_fail=modified_fail=modified_calls=format_calls=info_calls=0;}
static void destroy(struct drm_framebuffer *fb){for(int i=0;i<fb->format->num_planes;i++)drm_gem_object_put(fb->obj[i]);kfree(fb);}
static void no_refs(void){for(int i=0;i<4;i++)CHECK(objects[i].refs==0);CHECK(live==0);}
int main(void)
{
 struct msm_kms_funcs funcs={get_format,check_modified};struct msm_kms kms={&funcs};struct msm_drm_private priv={&kms};struct drm_device dev={&priv,NULL};struct drm_file file={0};
 struct drm_format_info info={.format=0x1234,.num_planes=2,.cpp={4,2},.hsub=2,.vsub=2};
 struct drm_format_info other={.format=0x1234,.num_planes=1,.cpp={1},.hsub=1,.vsub=1};resolved_info=&other;
 struct drm_mode_fb_cmd2 cmd={.width=5,.height=3,.pixel_format=0x1234,.handles={1,2,3,4},.pitches={20,6,6,6},.offsets={7,11}};
 struct drm_framebuffer *fb;reset();objects[0].size=67;objects[1].size=23;
 fb=callbacks.fb_create(&dev,&file,&info,&cmd);CHECK(!IS_ERR(fb));CHECK(fb->format==&info&&info_calls==0&&lookups==2&&live==1);CHECK(fb->width==5&&fb->height==3&&fb->pitches[1]==6&&fb->offsets[1]==11);destroy(fb);no_refs();
 reset();objects[1].size=22;fb=callbacks.fb_create(&dev,&file,&info,&cmd);CHECK(IS_ERR(fb)&&PTR_ERR(fb)==-EINVAL);no_refs();
 for(int n=1;n<=4;n++){reset();info.num_planes=n;fb=callbacks.fb_create(&dev,&file,&info,&cmd);CHECK(!IS_ERR(fb)&&lookups==n);for(int i=0;i<n;i++)CHECK(fb->obj[i]==&objects[i]);destroy(fb);no_refs();}
 info.num_planes=2;
 for(int failure=1;failure<=2;failure++){reset();lookup_fail=failure;fb=callbacks.fb_create(&dev,&file,&info,&cmd);CHECK(IS_ERR(fb)&&PTR_ERR(fb)==-ENXIO&&put_count==failure-1);no_refs();}
 reset();alloc_fail=1;fb=callbacks.fb_create(&dev,&file,&info,&cmd);CHECK(IS_ERR(fb)&&PTR_ERR(fb)==-ENOMEM);no_refs();
 reset();format_fail=1;fb=callbacks.fb_create(&dev,&file,&info,&cmd);CHECK(IS_ERR(fb)&&PTR_ERR(fb)==-EINVAL);no_refs();
 reset();init_fail=-EIO;fb=callbacks.fb_create(&dev,&file,&info,&cmd);CHECK(IS_ERR(fb)&&PTR_ERR(fb)==-EIO);no_refs();
 reset();cmd.flags=DRM_MODE_FB_MODIFIERS;cmd.modifier[0]=0x100;fb=callbacks.fb_create(&dev,&file,&info,&cmd);CHECK(!IS_ERR(fb)&&modified_calls==1&&fb->modifier==0x100&&fb->format==&info);destroy(fb);no_refs();
 reset();modified_fail=-ERANGE;fb=callbacks.fb_create(&dev,&file,&info,&cmd);CHECK(IS_ERR(fb)&&PTR_ERR(fb)==-ERANGE&&modified_calls==1);no_refs();
 reset();funcs.check_modified_format=NULL;fb=callbacks.fb_create(&dev,&file,&info,&cmd);CHECK(IS_ERR(fb)&&PTR_ERR(fb)==-EINVAL);no_refs();funcs.check_modified_format=check_modified;
 /* Internal caller owns its borrowed array; initializer must not acquire or put. */
 reset();resolved_info=&info;struct drm_gem_object *bos[]={&objects[0],&objects[1]};objects[0].refs=objects[1].refs=1;
 fb=msm_framebuffer_init(&dev,&cmd,bos);CHECK(!IS_ERR(fb)&&info_calls==1&&seen_fourcc==cmd.pixel_format&&seen_modifier==cmd.modifier[0]&&lookups==0&&put_count==0);destroy(fb);no_refs();
 reset();objects[0].refs=objects[1].refs=1;init_fail=-ENOSPC;fb=msm_framebuffer_init(&dev,&cmd,bos);CHECK(IS_ERR(fb)&&PTR_ERR(fb)==-ENOSPC&&objects[0].refs==1&&objects[1].refs==1&&put_count==0);drm_gem_object_put(bos[0]);drm_gem_object_put(bos[1]);no_refs();
 reset();cmd.flags=0;cmd.modifier[0]=0;cmd.handles[1]=1;fb=callbacks.fb_create(&dev,&file,&info,&cmd);CHECK(!IS_ERR(fb)&&objects[0].refs==2);destroy(fb);no_refs();cmd.handles[1]=2;
 reset();fb=callbacks.fb_create(&dev,&file,NULL,&cmd);CHECK(IS_ERR(fb)&&!lookups);no_refs();
 for(int n=0;n<=5;n+=5){reset();info.num_planes=n;fb=callbacks.fb_create(&dev,&file,&info,&cmd);CHECK(IS_ERR(fb)&&!lookups);no_refs();}info.num_planes=2;
 reset();cmd.width=0;fb=callbacks.fb_create(&dev,&file,&info,&cmd);CHECK(IS_ERR(fb)&&PTR_ERR(fb)==-EINVAL);no_refs();cmd.width=5;
 reset();info.format++;fb=callbacks.fb_create(&dev,&file,&info,&cmd);CHECK(IS_ERR(fb)&&PTR_ERR(fb)==-EINVAL);no_refs();info.format--;
 /* Size >4GiB must not wrap to a small accepted backing object. */
 reset();info.num_planes=1;cmd.width=1;cmd.height=3;cmd.pitches[0]=UINT_MAX;cmd.offsets[0]=0;objects[0].size=8;
 fb=callbacks.fb_create(&dev,&file,&info,&cmd);CHECK(IS_ERR(fb)&&PTR_ERR(fb)==-EINVAL);no_refs();
 reset();objects[0].size=2ULL*UINT_MAX+4;fb=callbacks.fb_create(&dev,&file,&info,&cmd);CHECK(!IS_ERR(fb));destroy(fb);no_refs();
 printf("PASS %d framebuffer checks: core format identity, modifiers, rounded extents, wide size and GEM reference accounting; driver leaves modeled\n",checks);return 0;
}
