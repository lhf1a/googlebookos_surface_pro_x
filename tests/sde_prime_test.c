/* SPDX-License-Identifier: GPL-2.0-only */
/* Production/core bodies; DMA, allocation, locks and enclosing types modeled. */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <errno.h>
typedef uint32_t u32; typedef uint64_t u64; typedef uint8_t u8; typedef uint64_t dma_addr_t;
#define PAGE_SHIFT 12
#define PAGE_SIZE 4096UL
#define PAGE_ALIGN(x) (((x)+4095UL)&~4095UL)
#define U8_MAX UINT8_MAX
#define GFP_KERNEL 0
#define VM_MAP 0
#define PAGE_KERNEL 0
#define DMA_BIDIRECTIONAL 0
#define MSM_BO_WC 2
#define MSM_BO_UNCACHED 1
#define MSM_BO_CACHED 4
#define MSM_BO_CACHE_MASK 7
#define MSM_MADV_WILLNEED 0
#define MSM_SMMU_DOMAIN_UNSECURE 0
#define MSM_SMMU_DOMAIN_SECURE 2
#define ION_FLAG_CP_SEC_DISPLAY 1
#define ION_FLAG_CP_CAMERA_PREVIEW 2
#define ION_FLAG_CP_PIXEL 4
#define ION_FLAG_CACHED 8
#define DMA_ATTR_IOMMU_USE_LLC_NWA 1
#define DMA_ATTR_DELAYED_UNMAP 2
#define DMA_ATTR_SKIP_CPU_SYNC 4
#define DMA_ATTR_IOMMU_USE_UPSTREAM_HINT 8
#define ERR_PTR(x) ((void *)(intptr_t)(x))
#define PTR_ERR(x) ((long)(intptr_t)(x))
#define ERR_CAST(x) ((void *)(x))
#define IS_ERR(x) ((uintptr_t)(x)>=(uintptr_t)-4095)
#define IS_ERR_OR_NULL(x) (!(x)||IS_ERR(x))
#define check_add_overflow(a,b,p) __builtin_add_overflow(a,b,p)
#define check_mul_overflow(a,b,p) __builtin_mul_overflow(a,b,p)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
static int scenarios, warnings;
#define CHECK(x) do { if (!(x)) {fprintf(stderr,"FAIL scenario %d line %d: %s\n",scenarios,__LINE__,#x);exit(1);} } while(0)
static int warn(bool x) { if(x)warnings++;return x; }
#define WARN_ON(x) warn(!!(x))
static void log_ignored(const char *f,...) {(void)f;}
#define DRM_ERROR(...) log_ignored(__VA_ARGS__)
#define DRM_DEBUG(...) log_ignored(__VA_ARGS__)
#define dev_err(d,...) ((void)(d),log_ignored(__VA_ARGS__))
#define SDE_EVT32(...) ((void)0)
struct list_head {struct list_head *next,*prev;};
#define INIT_LIST_HEAD(h) ((h)->next=(h),(h)->prev=(h))
static void list_add_tail(struct list_head *n,struct list_head *h){n->prev=h->prev;n->next=h;h->prev->next=n;h->prev=n;}
static void list_del(struct list_head *n){n->prev->next=n->next;n->next->prev=n->prev;INIT_LIST_HEAD(n);}
#define list_for_each_entry(p,h,m) for(p=container_of((h)->next,__typeof__(*p),m);&(p)->m!=(h);p=container_of((p)->m.next,__typeof__(*p),m))
#define list_for_each_entry_safe(p,t,h,m) for(p=container_of((h)->next,__typeof__(*p),m),t=container_of((p)->m.next,__typeof__(*p),m);&(p)->m!=(h);p=t,t=container_of((t)->m.next,__typeof__(*t),m))
struct mutex {int held;};
static void mutex_init(struct mutex *m){m->held=0;}
static void mutex_lock(struct mutex *m){CHECK(!m->held);m->held=1;}
static void mutex_unlock(struct mutex *m){CHECK(m->held);m->held=0;}
static int mutex_is_locked(struct mutex *m){return m->held;}
struct kref {int refs;};
static void kref_get(struct kref *r){CHECK(r->refs>0);r->refs++;}
static bool kref_get_unless_zero(struct kref *r){if(!r->refs)return false;kref_get(r);return true;}
static void kref_put(struct kref *r,void (*destroy)(struct kref *)){CHECK(r->refs>0);if(!--r->refs)destroy(r);}
struct dma_resv {int held,initialized;};
struct iosys_map {void *vaddr;bool is_iomem;};
static void iosys_map_clear(struct iosys_map *m){m->vaddr=NULL;m->is_iomem=false;}
static bool iosys_map_is_null(struct iosys_map *m){return !m->vaddr;}
struct device {int id;};
struct scatterlist {dma_addr_t dma_address;size_t dma_length;struct scatterlist *next;};
struct sg_table {struct scatterlist *sgl;unsigned int nents,orig_nents;};
#define sg_dma_address(s) ((s)->dma_address)
#define sg_dma_len(s) ((s)->dma_length)
#define for_each_sg(first,sg,n,i) for ((i)=0,(sg)=(first); (i)<(n); (i)++,(sg)=(sg)->next)
struct dma_buf_ops {int (*begin_cpu_access)(void);};
struct dma_buf {void *priv;const struct dma_buf_ops *ops;size_t size;struct dma_resv *resv;int refs;};
struct dma_buf_attachment {struct dma_buf *dmabuf;struct device *dev;unsigned long dma_map_attrs;};
struct drm_device;struct drm_gem_object;struct msm_gem_address_space;
struct drm_gem_object_funcs {int (*vmap)(struct drm_gem_object *,struct iosys_map *);void (*vunmap)(struct drm_gem_object *,struct iosys_map *);};
struct drm_gem_object {struct drm_device *dev;size_t size;struct dma_buf_attachment *import_attach;struct dma_resv *resv,_resv;const struct drm_gem_object_funcs *funcs;int refs;struct dma_buf *dma_buf;};
struct driver {struct drm_gem_object *(*gem_prime_import_sg_table)(struct drm_device *,struct dma_buf_attachment *,struct sg_table *);};
struct msm_kms;struct kms_funcs {struct device *(*get_address_space_device)(struct msm_kms *,u32);struct msm_gem_address_space *(*get_address_space)(struct msm_kms *,u32);};
struct msm_kms {const struct kms_funcs *funcs;};
struct msm_drm_private {struct msm_kms *kms;struct mutex gem_lock;struct list_head inactive_list;};
struct drm_device {struct device *dev;struct driver *driver;void *dev_private;struct mutex struct_mutex,object_name_lock;};
struct page {int unused;};
struct msm_mmu;struct mmu_funcs {int (*map_dma_buf)(struct msm_mmu *,struct sg_table *,int,unsigned int);void (*unmap_dma_buf)(struct msm_mmu *,struct sg_table *,int,unsigned int);bool (*is_domain_secure)(struct msm_mmu *);struct device *(*get_dev)(struct msm_mmu *);void (*destroy)(struct msm_mmu *);};
struct msm_mmu {struct mmu_funcs *funcs;struct device *dev;bool secure;};
struct msm_gem_object;
struct aspace_ops {void (*add_to_active)(struct msm_gem_address_space *,struct msm_gem_object *);};
struct drm_mm {int unused;};
struct msm_gem_address_space {struct kref kref;struct mutex list_lock;bool domain_attached;struct msm_mmu *mmu;struct aspace_ops *ops;struct drm_mm mm;};
struct msm_gem_vma {struct list_head list;struct msm_gem_address_space *aspace;uint64_t iova;bool mapped;};
struct msm_gem_object {struct drm_gem_object base;u32 flags;u8 madv,vmap_count;struct mutex lock;struct list_head mm_list,submit_entry,vmas,iova_list;struct msm_gem_address_space *aspace,*import_aspace;bool in_active_list,obj_dirty,aspace_ref;struct dma_resv *resv,_resv;struct sg_table *sgt;struct page **pages;void *vaddr;struct iosys_map import_map;int import_error;};
#define to_msm_bo(x) container_of(x,struct msm_gem_object,base)
#include "actual-definitions.h"
#ifndef LEGACY_MODEL
int msm_gem_prime_vmap(struct drm_gem_object *,struct iosys_map *);
void msm_gem_prime_vunmap(struct drm_gem_object *,struct iosys_map *);
static const struct drm_gem_object_funcs msm_gem_object_funcs={msm_gem_prime_vmap,msm_gem_prime_vunmap};
#else
static const struct drm_gem_object_funcs msm_gem_object_funcs __attribute__((unused))={0};
#endif
static const struct dma_buf_ops drm_gem_prime_dmabuf_ops={0},foreign_ops={0};
static int alloc_fail,allocations,live_alloc,attach_error,map_error,live_attach,live_maps,ref_inits,ref_finis;
static int maps,unmaps,attaches,detaches,mmu_maps,mmu_unmaps,mmu_error;
static int cpu_begin_error,cpu_end_error,cpu_map_error,cpu_begins,cpu_ends,cpu_maps,cpu_unmaps;
static bool iomem_map;
static bool domain_alive=true;
static int domain_destroys;
static struct msm_gem_address_space *kms_aspace;
static struct sg_table *mapped_sg;
static struct device attach_device={1},other_device={2};
static char cpu_memory[4096];
static int platform_bus_type __attribute__((unused));
static void dma_resv_lock(struct dma_resv *r,void *ctx){(void)ctx;CHECK(!r->held);r->held=1;}
static void dma_resv_unlock(struct dma_resv *r){CHECK(r->held);r->held=0;}
static void dma_resv_assert_held(struct dma_resv *r){CHECK(r->held);}
static void iosys_map_set_vaddr(struct iosys_map *m,void *p){*m=(struct iosys_map){p,false};}
static void *kzalloc(size_t n,int flags){(void)flags;allocations++;if(alloc_fail==allocations)return NULL;void *p=calloc(1,n);CHECK(p);live_alloc++;return p;}
static void kfree(void *p){if(p){live_alloc--;free(p);}}
static void kvfree(void *p){kfree(p);}
static void dma_resv_init(struct dma_resv *r){r->initialized=1;ref_inits++;}
static void dma_resv_fini(struct dma_resv *r){CHECK(r->initialized);r->initialized=0;ref_finis++;}
static void get_dma_buf(struct dma_buf *b){CHECK(b->refs>0);b->refs++;}
static void dma_buf_put(struct dma_buf *b){CHECK(b->refs>1);b->refs--;}
static struct dma_buf_attachment *dma_buf_attach(struct dma_buf *b,struct device *d){attaches++;if(attach_error)return ERR_PTR(attach_error);struct dma_buf_attachment *a=malloc(sizeof(*a));CHECK(a);*a=(struct dma_buf_attachment){b,d,0};live_attach++;return a;}
static void dma_buf_detach(struct dma_buf *b,struct dma_buf_attachment *a){CHECK(domain_alive&&a&&a->dmabuf==b);detaches++;live_attach--;free(a);}
static struct sg_table *dma_buf_map_attachment(struct dma_buf_attachment *a,int d){CHECK(a&&d==DMA_BIDIRECTIONAL);maps++;if(map_error)return ERR_PTR(map_error);live_maps++;return mapped_sg;}
static void dma_buf_unmap_attachment(struct dma_buf_attachment *a,struct sg_table *s,int d){CHECK(domain_alive&&a&&s==mapped_sg&&d==DMA_BIDIRECTIONAL);unmaps++;live_maps--;}
static struct sg_table *dma_buf_map_attachment_unlocked(struct dma_buf_attachment *a,int d){CHECK(!a->dmabuf->resv->held);a->dmabuf->resv->held=1;struct sg_table *s=dma_buf_map_attachment(a,d);a->dmabuf->resv->held=0;return s;}
static void dma_buf_unmap_attachment_unlocked(struct dma_buf_attachment *a,struct sg_table *s,int d){CHECK(!a->dmabuf->resv->held);a->dmabuf->resv->held=1;dma_buf_unmap_attachment(a,s,d);a->dmabuf->resv->held=0;}
static int dma_buf_get_flags(struct dma_buf *b,unsigned long *f){(void)b;*f=0;return 0;}
static int iommu_present(void *p){(void)p;return 1;}
static int dma_buf_begin_cpu_access(struct dma_buf *b,int d){CHECK(!b->resv->held&&d==0);cpu_begins++;return cpu_begin_error;}
static int dma_buf_end_cpu_access(struct dma_buf *b,int d){CHECK(!b->resv->held&&d==0);cpu_ends++;return cpu_end_error;}
static int dma_buf_vmap(struct dma_buf *b,struct iosys_map *map){CHECK(b->resv->held);cpu_maps++;if(cpu_map_error)return cpu_map_error;*map=(struct iosys_map){cpu_memory,iomem_map};return 0;}
static void dma_buf_vunmap(struct dma_buf *b,struct iosys_map *map){CHECK(b->resv->held&&map->vaddr==cpu_memory);cpu_unmaps++;}
static int dma_buf_vmap_unlocked(struct dma_buf *b,struct iosys_map *map){dma_resv_lock(b->resv,NULL);int r=dma_buf_vmap(b,map);dma_resv_unlock(b->resv);return r;}
static void dma_buf_vunmap_unlocked(struct dma_buf *b,struct iosys_map *map){dma_resv_lock(b->resv,NULL);dma_buf_vunmap(b,map);dma_resv_unlock(b->resv);}
static void *legacy_dma_buf_vmap(struct dma_buf *b){struct iosys_map m;return dma_buf_vmap_unlocked(b,&m)?NULL:m.vaddr;}
static void legacy_dma_buf_vunmap(struct dma_buf *b,void *v){struct iosys_map m={v,false};dma_buf_vunmap_unlocked(b,&m);}
static unsigned long drm_prime_get_contiguous_size(struct sg_table *t){struct scatterlist *s=t->sgl;uint64_t expected=s->dma_address;unsigned long total=0;for(unsigned int i=0;i<t->nents&&s;i++,s=s->next){if(!s->dma_length||s->dma_address!=expected)break;total+=s->dma_length;expected+=s->dma_length;}return total;}
static void drm_gem_private_object_init(struct drm_device *d,struct drm_gem_object *o,size_t n){o->dev=d;o->size=n;o->refs=1;dma_resv_init(&o->_resv);if(!o->resv)o->resv=&o->_resv;}
static void drm_gem_object_release(struct drm_gem_object *o){dma_resv_fini(&o->_resv);}
static void drm_gem_object_get(struct drm_gem_object *o){CHECK(o->refs>0);o->refs++;}
static void msm_gem_free_object(struct drm_gem_object *o);
static void drm_gem_object_put_unlocked(struct drm_gem_object *o){if(o&&!--o->refs)msm_gem_free_object(o);}
static void drm_gem_object_put(struct drm_gem_object *o){drm_gem_object_put_unlocked(o);}
void msm_gem_address_space_put(struct msm_gem_address_space *);
static void drm_mm_takedown(struct drm_mm *m){(void)m;}
static void mmu_destroy(struct msm_mmu *m){CHECK(m&&domain_alive&&!live_maps&&!live_attach);domain_alive=false;domain_destroys++;}
static struct device *msm_gem_get_aspace_device(struct msm_gem_address_space *a){return a&&a->mmu?a->mmu->dev:NULL;}
static void active_add(struct msm_gem_address_space *a,struct msm_gem_object *o){CHECK(a->list_lock.held);o->in_active_list=true;}
static void msm_gem_add_obj_to_aspace_active_list(struct msm_gem_address_space *a,struct drm_gem_object *o){active_add(a,to_msm_bo(o));}
static void msm_gem_remove_obj_from_aspace_active_list(struct msm_gem_address_space *a,struct drm_gem_object *o){CHECK(a->list_lock.held);to_msm_bo(o)->in_active_list=false;}
static bool is_active(struct msm_gem_object *o){(void)o;return false;}
static bool is_vunmapable(struct msm_gem_object *o){return !o->vmap_count&&o->vaddr;}
static struct page **get_pages(struct drm_gem_object *o){return to_msm_bo(o)->pages;}
static void put_pages(struct drm_gem_object *o){(void)o;}
static void *vmap(struct page **p,size_t n,int f,int prot){(void)p;(void)n;(void)f;(void)prot;return cpu_memory;}
static void vunmap(void *v){CHECK(v==cpu_memory);}
static int pgprot_writecombine(int p){return p;}
static int mmu_map(struct msm_mmu *m,struct sg_table *s,int d,unsigned int f){CHECK(m&&s&&d==0&&(f&MSM_BO_EXTBUF));mmu_maps++;return mmu_error;}
static void mmu_unmap(struct msm_mmu *m,struct sg_table *s,int d,unsigned int f){CHECK(m&&s&&d==0&&(f&MSM_BO_EXTBUF));mmu_unmaps++;}
static bool secure(struct msm_mmu *m){return m->secure;}
static int smmu_aspace_map_vma(struct msm_gem_address_space *,struct msm_gem_vma *,struct sg_table *,int,unsigned int);
static void smmu_aspace_unmap_vma(struct msm_gem_address_space *,struct msm_gem_vma *,struct sg_table *,unsigned int);
static int msm_gem_map_vma(struct msm_gem_address_space *a,struct msm_gem_vma *v,struct sg_table *s,int n,unsigned int f){return smmu_aspace_map_vma(a,v,s,n,f);}
static void msm_gem_unmap_vma(struct msm_gem_address_space *a,struct msm_gem_vma *v,struct sg_table *s,unsigned int f){smmu_aspace_unmap_vma(a,v,s,f);}
static struct drm_gem_object *msm_gem_import(struct drm_device *,struct dma_buf *,struct sg_table *);
static void drm_prime_gem_destroy(struct drm_gem_object *,struct sg_table *);
static void *msm_gem_get_vaddr(struct drm_gem_object *);
#ifndef LEGACY_MODEL
struct drm_prime_file_private {struct mutex lock;struct dma_buf *cached;};
struct drm_file {struct drm_prime_file_private prime;};
static struct drm_gem_object *lookup_object;
static struct drm_gem_object *drm_gem_object_lookup(struct drm_file *f,uint32_t handle){CHECK(f->prime.lock.held&&handle==1);drm_gem_object_get(lookup_object);return lookup_object;}
static struct dma_buf *drm_prime_lookup_buf_by_handle(struct drm_prime_file_private *p,uint32_t handle){CHECK(p->lock.held&&handle==1);return p->cached;}
static int drm_prime_add_buf_handle(struct drm_prime_file_private *p,struct dma_buf *b,uint32_t handle){CHECK(p->lock.held&&handle==1&&!p->cached);get_dma_buf(b);p->cached=b;return 0;}
static struct dma_buf *export_and_register_object(struct drm_device *d,struct drm_gem_object *o,uint32_t flags){(void)d;(void)o;(void)flags;CHECK(false);return ERR_PTR(-EINVAL);}
#endif
#include "actual-functions.h"
static void *msm_gem_get_vaddr(struct drm_gem_object *o){return get_vaddr(o,MSM_MADV_WILLNEED);}
static struct device *select_device(struct msm_kms *k,u32 domain){CHECK(k&&domain==0);return &attach_device;}
static struct msm_gem_address_space *select_aspace(struct msm_kms *k,u32 domain){CHECK(k&&domain==0);return kms_aspace;}
static void reset(void){scenarios++;CHECK(!live_alloc&&!live_attach&&!live_maps);allocations=alloc_fail=attach_error=map_error=0;maps=unmaps=attaches=detaches=mmu_maps=mmu_unmaps=mmu_error=0;cpu_begin_error=cpu_end_error=cpu_map_error=cpu_begins=cpu_ends=cpu_maps=cpu_unmaps=0;iomem_map=false;warnings=0;}
static int get_iova(struct drm_gem_object *o,struct msm_gem_address_space *a,uint64_t *i){struct msm_gem_object *m=to_msm_bo(o);mutex_lock(&m->lock);int r=msm_gem_get_iova_locked(o,a,i);mutex_unlock(&m->lock);return r;}
int main(void)
{
    struct kms_funcs funcs={select_device,select_aspace};struct msm_kms kms={&funcs};struct msm_drm_private priv={.kms=&kms};
    struct driver driver={msm_gem_prime_import_sg_table};struct drm_device dev={.dev=&attach_device,.driver=&driver,.dev_private=&priv};
    struct dma_resv resv={0};struct scatterlist sg={0x20000,4096,NULL};struct sg_table sgt={&sg,1,1};
    struct dma_buf buf={.priv=NULL,.ops=&foreign_ops,.size=4096,.resv=&resv,.refs=1};
    struct mmu_funcs mf={mmu_map,mmu_unmap,secure,NULL,mmu_destroy};struct msm_mmu mmu={&mf,&attach_device,false};struct aspace_ops aops={active_add};
    struct msm_gem_address_space aspace={.kref={1},.domain_attached=true,.mmu=&mmu,.ops=&aops};uint64_t iova;struct drm_gem_object *obj;struct msm_gem_object *m;
    INIT_LIST_HEAD(&priv.inactive_list);mapped_sg=&sgt;kms_aspace=&aspace;
    /* The first case is the original reproducible leaked-reference path. */
    reset();attach_error=-EIO;obj=msm_gem_prime_import(&dev,&buf);CHECK(IS_ERR(obj)&&PTR_ERR(obj)==-EIO);CHECK(buf.refs==1&&!live_attach&&!live_maps);
    reset();map_error=-ETIMEDOUT;obj=msm_gem_prime_import(&dev,&buf);CHECK(IS_ERR(obj)&&PTR_ERR(obj)==-ETIMEDOUT);CHECK(buf.refs==1&&detaches==1);
    reset();alloc_fail=1;obj=msm_gem_prime_import(&dev,&buf);CHECK(IS_ERR(obj)&&PTR_ERR(obj)==-ENOMEM);CHECK(buf.refs==1&&unmaps==1&&detaches==1);
    reset();buf.priv=(void *)1;obj=msm_gem_prime_import(&dev,&buf);CHECK(!IS_ERR(obj)&&obj!=(void *)1);drm_gem_object_put_unlocked(obj);buf.priv=NULL;
    reset();priv.kms=NULL;obj=msm_gem_prime_import(&dev,&buf);CHECK(IS_ERR(obj)&&PTR_ERR(obj)==-EOPNOTSUPP&&buf.refs==1&&!attaches);priv.kms=&kms;
    reset();driver.gem_prime_import_sg_table=NULL;obj=msm_gem_prime_import(&dev,&buf);CHECK(IS_ERR(obj)&&PTR_ERR(obj)==-EINVAL&&!attaches);driver.gem_prime_import_sg_table=msm_gem_prime_import_sg_table;
    for(int bad=0;bad<5;bad++){
        reset();size_t n=buf.size,len=sg.dma_length;unsigned int entries=sgt.nents;dma_addr_t address=sg.dma_address;
        if(bad==0)buf.size=0;
        if(bad==1)buf.size=SIZE_MAX;
        if(bad==2)sgt.nents=0;
        if(bad==3)sg.dma_length=2048;
        if(bad==4)sg.dma_address=UINT64_MAX-2047;
        obj=msm_gem_prime_import(&dev,&buf);CHECK(IS_ERR(obj)&&buf.refs==1&&unmaps==1&&detaches==1);
        buf.size=n;sg.dma_length=len;sgt.nents=entries;sg.dma_address=address;
    }
    reset();obj=msm_gem_prime_import(&dev,&buf);CHECK(!IS_ERR(obj));m=to_msm_bo(obj);CHECK(obj->resv==buf.resv&&m->sgt==&sgt&&obj->import_attach->dev==&attach_device&&buf.refs==2&&maps==1);drm_gem_object_put_unlocked(obj);CHECK(buf.refs==1&&unmaps==1&&detaches==1&&ref_inits==ref_finis);
    for(int zero=0;zero<2;zero++){
        reset();sg.dma_address=zero?0:0x20000;obj=msm_gem_prime_import(&dev,&buf);CHECK(!IS_ERR(obj));iova=0xdead;
        CHECK(msm_gem_get_and_pin_iova(obj,&aspace,&iova)==0&&iova==sg.dma_address&&aspace.kref.refs==3&&mmu_maps==1);
        CHECK(get_iova(obj,&aspace,&iova)==0&&aspace.kref.refs==3&&mmu_maps==1);drm_gem_object_put_unlocked(obj);CHECK(aspace.kref.refs==1&&mmu_unmaps==1&&buf.refs==1);
    }sg.dma_address=0x20000;
    for(int bad=0;bad<6;bad++){
        reset();obj=msm_gem_prime_import(&dev,&buf);CHECK(!IS_ERR(obj));m=to_msm_bo(obj);iova=0xdead;
        if(bad==0)mmu.dev=&other_device;
        if(bad==1)mmu.secure=true;
        if(bad==2)aspace.domain_attached=false;
        if(bad==3)m->obj_dirty=true;
        if(bad==4)mmu_error=-EIO;
        if(bad==5)alloc_fail=allocations+1;
        int r=get_iova(obj,&aspace,&iova);CHECK(r==(bad<4?-EOPNOTSUPP:bad==4?-EIO:-ENOMEM));CHECK(iova==0xdead&&buf.refs==2&&live_maps==1&&aspace.kref.refs==2&&!unmaps&&!detaches);
        mmu.dev=&attach_device;mmu.secure=false;aspace.domain_attached=true;m->obj_dirty=false;drm_gem_object_put_unlocked(obj);CHECK(aspace.kref.refs==1&&buf.refs==1);
    }
    for(int bad=0;bad<4;bad++){
        reset();obj=msm_gem_prime_import(&dev,&buf);CHECK(!IS_ERR(obj));m=to_msm_bo(obj);
        if(bad==0)cpu_begin_error=-EACCES;
        if(bad==1)cpu_map_error=-ENOMEM;
        if(bad==2)iomem_map=true;
        if(bad==3)cpu_end_error=-EIO;
        void *v=get_vaddr(obj,MSM_MADV_WILLNEED);
        if(bad<3){CHECK(IS_ERR(v)&&PTR_ERR(v)==(bad==0?-EACCES:bad==1?-ENOMEM:-EOPNOTSUPP));CHECK(!m->vmap_count&&!m->vaddr&&cpu_ends==(bad?1:0));}
        else {CHECK(v==cpu_memory);CHECK(get_vaddr(obj,MSM_MADV_WILLNEED)==v&&cpu_begins==1);msm_gem_put_vaddr(obj);CHECK(!cpu_ends);msm_gem_put_vaddr(obj);CHECK(cpu_ends==1&&cpu_unmaps==1&&m->import_error==-EIO);CHECK(get_iova(obj,&aspace,&iova)==-EIO);}
        drm_gem_object_put_unlocked(obj);CHECK(buf.refs==1&&ref_inits==ref_finis);
    }
    reset();obj=msm_gem_prime_import(&dev,&buf);CHECK(!IS_ERR(obj));m=to_msm_bo(obj);CHECK(get_vaddr(obj,0)==cpu_memory);CHECK(get_iova(obj,&aspace,&iova)==-EBUSY);msm_gem_put_vaddr(obj);CHECK(!m->vaddr&&cpu_ends==1&&cpu_unmaps==1);CHECK(get_iova(obj,&aspace,&iova)==0);drm_gem_object_put_unlocked(obj);CHECK(aspace.kref.refs==1&&buf.refs==1);
#ifndef LEGACY_MODEL
    for(int use_vma=0;use_vma<2;use_vma++){
        reset();struct msm_gem_address_space *last=kzalloc(sizeof(*last),0);*last=aspace;last->kref.refs=1;kms_aspace=last;
        obj=msm_gem_prime_import(&dev,&buf);CHECK(!IS_ERR(obj)&&last->kref.refs==2);
        if(use_vma)CHECK(get_iova(obj,last,&iova)==0&&last->kref.refs==3);
        msm_gem_address_space_put(last);CHECK(domain_alive);drm_gem_object_put_unlocked(obj);CHECK(!domain_alive&&buf.refs==1&&unmaps==1&&detaches==1&&domain_destroys==use_vma+1);
        domain_alive=true;kms_aspace=&aspace;
    }
    reset();obj=msm_gem_prime_import(&dev,&buf);CHECK(!IS_ERR(obj));struct iosys_map prime_map={0};
    CHECK(drm_gem_vmap(obj,&prime_map)==0&&prime_map.vaddr==cpu_memory&&!resv.held&&cpu_begins==0);drm_gem_vunmap(obj,&prime_map);CHECK(iosys_map_is_null(&prime_map)&&!resv.held&&cpu_ends==0&&cpu_unmaps==1);drm_gem_object_put_unlocked(obj);
    reset();obj=msm_gem_prime_import(&dev,&buf);CHECK(!IS_ERR(obj));cpu_map_error=-ENOMEM;CHECK(drm_gem_vmap(obj,&prime_map)==-ENOMEM&&!resv.held&&cpu_begins==0);drm_gem_object_put_unlocked(obj);
    reset();obj=msm_gem_prime_import(&dev,&buf);CHECK(!IS_ERR(obj));struct drm_file file={0};lookup_object=obj;struct dma_buf *again=drm_gem_prime_handle_to_dmabuf(&dev,&file,1,0);CHECK(again==&buf&&obj->refs==1&&buf.refs==4&&!cpu_maps);dma_buf_put(again);dma_buf_put(file.prime.cached);file.prime.cached=NULL;drm_gem_object_put_unlocked(obj);CHECK(buf.refs==1);
#endif
    reset();CHECK(ref_inits==ref_finis);printf("PASS %d actual-function PRIME owner scenarios; DMA/PM/locks modeled, no hardware proof\n",scenarios);
    return 0;
}
