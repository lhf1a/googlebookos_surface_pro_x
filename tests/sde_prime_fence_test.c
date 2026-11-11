/* SPDX-License-Identifier: GPL-2.0-only */
/* Actual 6.18 fence helper and actual SDE state/property/prepare functions. */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define SDE_DRM_FB_NON_SEC 0
#define SDE_DRM_FB_SEC 1
#define SDE_DRM_FB_NON_SEC_DIR_TRANS 2
#define SDE_DRM_FB_SEC_DIR_TRANS 3
#define MSM_SMMU_DOMAIN_UNSECURE 0
#define MSM_SMMU_DOMAIN_SECURE 2
#define PLANE_PROP_FB_TRANSLATION_MODE 1
static int scenarios,live_fences,lookups,lookup_error,chain_error,maps,layouts;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL scenario %d line %d: %s\n",scenarios,__LINE__,#x);exit(1);}}while(0)
static void log_ignored(const char *fmt,...){(void)fmt;}
#define SDE_ERROR(...) log_ignored(__VA_ARGS__)
#define SDE_ERROR_PLANE(p,...) ((void)(p),log_ignored(__VA_ARGS__))
#define SDE_DEBUG_PLANE(p,...) ((void)(p),log_ignored(__VA_ARGS__))
#define SDE_EVT32(...) ((void)0)
#define DRMID(x) 0
struct dma_fence {int refs;bool chain;struct dma_fence *prev,*next;};
struct dma_fence_chain {struct dma_fence base;};
enum dma_resv_usage {DMA_RESV_USAGE_KERNEL,DMA_RESV_USAGE_WRITE};
struct dma_resv {struct dma_fence *kernel,*write;};
struct drm_gem_object {struct dma_resv *resv;};
struct drm_format_info {uint8_t num_planes;};
struct drm_framebuffer {struct {unsigned int id;}base;struct drm_format_info *format;struct drm_gem_object *obj[4];};
struct drm_plane_state {struct drm_framebuffer *fb;struct dma_fence *fence;};
struct drm_plane {int unused;};
struct msm_gem_address_space {bool domain_attached;};
struct sde_kms {struct msm_gem_address_space *aspace[4];};
struct sde_plane {struct drm_plane base;struct sde_kms *kms;int pipe;};
struct sde_plane_state {struct drm_plane_state base;int mode;struct msm_gem_address_space *aspace;bool defer_prepare_fb;};
struct sde_hw_fmt_layout {int unused;};
#define to_sde_plane(p) container_of(p,struct sde_plane,base)
#define to_sde_plane_state(p) container_of(p,struct sde_plane_state,base)
static struct sde_kms *_sde_plane_get_kms(struct drm_plane *p){return to_sde_plane(p)->kms;}
static int sde_plane_get_property(struct sde_plane_state *s,int prop){CHECK(prop==PLANE_PROP_FB_TRANSLATION_MODE);return s->mode;}
static struct dma_fence *new_fence(void){struct dma_fence *f=calloc(1,sizeof(*f));CHECK(f);f->refs=1;live_fences++;return f;}
static struct dma_fence *dma_fence_get(struct dma_fence *f){if(f){CHECK(f->refs>0);f->refs++;}return f;}
static void dma_fence_put(struct dma_fence *f){if(!f)return;CHECK(f->refs>0);if(!--f->refs){if(f->chain){dma_fence_put(f->prev);dma_fence_put(f->next);}live_fences--;free(f);}}
static struct dma_fence_chain *dma_fence_chain_alloc(void){if(chain_error)return NULL;struct dma_fence_chain *c=calloc(1,sizeof(*c));CHECK(c);return c;}
static void dma_fence_chain_init(struct dma_fence_chain *c,struct dma_fence *p,struct dma_fence *n,int seq){CHECK(seq==1);c->base=(struct dma_fence){1,true,p,n};live_fences++;}
static enum dma_resv_usage queried[4];
static int dma_resv_get_singleton(struct dma_resv *r,enum dma_resv_usage u,struct dma_fence **out){CHECK(lookups<4);queried[lookups++]=u;if(lookup_error==lookups)return -EIO;*out=dma_fence_get(u==DMA_RESV_USAGE_KERNEL?r->kernel:r->write);return 0;}
static struct drm_gem_object *drm_gem_fb_get_obj(struct drm_framebuffer *fb,size_t i){CHECK(i<fb->format->num_planes);return fb->obj[i];}
static int msm_framebuffer_prepare(struct drm_framebuffer *f,struct msm_gem_address_space *a){CHECK(f&&a&&a->domain_attached);maps++;return 0;}
static int sde_format_populate_layout(struct msm_gem_address_space *a,struct drm_framebuffer *f,struct sde_hw_fmt_layout *l){CHECK(a&&f&&l);layouts++;return 0;}
static int msm_fb_obj_get_attrs(struct drm_gem_object *o,uint32_t *a,uint32_t *b,uint32_t *c,unsigned long *f){(void)o;(void)a;(void)b;(void)c;(void)f;return -EOPNOTSUPP;}
#include "actual-functions.h"
static void reset(void){scenarios++;lookups=lookup_error=chain_error=maps=layouts=0;memset(queried,0,sizeof(queried));}
int main(int argc,char **argv)
{
    struct dma_resv resv[3];struct drm_gem_object objs[3];struct drm_format_info format={3};struct drm_framebuffer fb={.format=&format};
    struct msm_gem_address_space ordinary={true},secure={true};struct sde_kms kms={.aspace={&ordinary,NULL,&secure,NULL}};struct sde_plane plane={.kms=&kms};
    struct sde_plane_state state={.base={.fb=&fb},.mode=SDE_DRM_FB_NON_SEC};
    for(int i=0;i<3;i++){resv[i]=(struct dma_resv){new_fence(),new_fence()};objs[i].resv=&resv[i];fb.obj[i]=&objs[i];}
    if(argc>1){
        (void)argv;reset();chain_error=1;state.base.fence=new_fence();int before=live_fences;
        CHECK(drm_gem_plane_helper_prepare_fb(&plane.base,&state.base)==-ENOMEM);
        dma_fence_put(state.base.fence);state.base.fence=NULL;
        for(int i=0;i<3;i++){dma_fence_put(resv[i].kernel);dma_fence_put(resv[i].write);}
        if(live_fences){fprintf(stderr,"FAIL actual core chain allocation path leaked %d fence(s), before=%d\n",live_fences,before);return 1;}
        return 0;
    }
    for(int mode=0;mode<4;mode++){
        reset();state.mode=mode;int r=_sde_plane_validate_fb(&plane,&state.base);CHECK(r==(mode==0?0:-EOPNOTSUPP));struct msm_gem_address_space *a=NULL;
        r=_sde_plane_get_aspace(&plane,&state,&a);CHECK(r==(mode==0?0:-EOPNOTSUPP));if(mode==0)CHECK(a==&ordinary);
    }state.mode=0;
    for(int explicit=0;explicit<2;explicit++){
        reset();state.base.fence=explicit?new_fence():NULL;CHECK(sde_plane_prepare_fb(&plane.base,&state.base)==0);CHECK(lookups==3&&maps==1&&layouts==1);
        for(int i=0;i<3;i++)CHECK(queried[i]==(explicit?DMA_RESV_USAGE_KERNEL:DMA_RESV_USAGE_WRITE));
        dma_fence_put(state.base.fence);state.base.fence=NULL;CHECK(live_fences==6);
    }
    reset();state.base.fence=new_fence();struct dma_fence *original=state.base.fence;lookup_error=2;CHECK(sde_plane_prepare_fb(&plane.base,&state.base)==-EIO);CHECK(state.base.fence==original&&original->refs==1&&!maps&&!layouts);dma_fence_put(original);state.base.fence=NULL;CHECK(live_fences==6);
    reset();struct drm_gem_object *saved=fb.obj[1];fb.obj[1]=NULL;CHECK(drm_gem_plane_helper_prepare_fb(&plane.base,&state.base)==-EINVAL);CHECK(!state.base.fence&&live_fences==6);fb.obj[1]=saved;
    reset();ordinary.domain_attached=false;CHECK(sde_plane_prepare_fb(&plane.base,&state.base)==0);CHECK(state.defer_prepare_fb&&lookups==3&&!maps&&!layouts);dma_fence_put(state.base.fence);state.base.fence=NULL;ordinary.domain_attached=true;CHECK(live_fences==6);
    for(int i=0;i<3;i++){dma_fence_put(resv[i].kernel);dma_fence_put(resv[i].write);}CHECK(!live_fences);
    printf("PASS %d actual core/SDE fence and translation-state scenarios; fence/format leaves modeled\n",scenarios);return 0;
}
