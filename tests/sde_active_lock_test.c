/* SPDX-License-Identifier: GPL-2.0-only */
/* Production/core bodies; DMA, allocation, locks and enclosing types modeled. */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdatomic.h>
#include <sched.h>
#include <time.h>
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
/* BO/MADV and private BO definitions are extracted into actual-definitions.h. */
#define MSM_SMMU_DOMAIN_UNSECURE 0
#define MSM_SMMU_DOMAIN_SECURE 2
/* Opaque legacy model tags; these values are not an ABI validation. */
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
static void list_move_tail(struct list_head *n,struct list_head *h){list_del(n);list_add_tail(n,h);}
#define list_for_each_entry(p,h,m) for(p=container_of((h)->next,__typeof__(*p),m);&(p)->m!=(h);p=container_of((p)->m.next,__typeof__(*p),m))
#define list_for_each_entry_safe(p,t,h,m) for(p=container_of((h)->next,__typeof__(*p),m),t=container_of((p)->m.next,__typeof__(*p),m);&(p)->m!=(h);p=t,t=container_of((t)->m.next,__typeof__(*t),m))
struct mutex {pthread_mutex_t native;_Atomic int held;int which;};
static _Atomic int race_enabled,list_held,obj_held,detach_wait_obj,free_wait_list;
static _Atomic int pin_pause,snapshot_pinned,original_put_done,callback_entered,callback_release,unregister_started,unregister_done;
static _Thread_local int actor;
static void mutex_init(struct mutex *m){pthread_mutex_init(&m->native,NULL);m->held=0;m->which=0;}
static void mutex_lock(struct mutex *m){
 if(atomic_load(&race_enabled)&&actor==1&&m->which==1)atomic_store(&detach_wait_obj,1);
 if(atomic_load(&race_enabled)&&actor==2&&m->which==2)atomic_store(&free_wait_list,1);
 struct timespec until;clock_gettime(CLOCK_REALTIME,&until);until.tv_sec+=1;
 int rc=pthread_mutex_timedlock(&m->native,&until);
 if(rc==ETIMEDOUT){fprintf(stderr,"ACTUAL_SOURCE_LOCK_TIMEOUT actor=%d waits_lock=%d list_held=%d obj_held=%d detach_wait_obj=%d free_wait_list=%d\n",actor,m->which,atomic_load(&list_held),atomic_load(&obj_held),atomic_load(&detach_wait_obj),atomic_load(&free_wait_list));_Exit(1);}
 CHECK(rc==0);m->held=1;
 if(atomic_load(&race_enabled)&&actor==1&&m->which==2){atomic_store(&list_held,1);while(!atomic_load(&obj_held))sched_yield();}
 if(atomic_load(&race_enabled)&&actor==2&&m->which==1)atomic_store(&obj_held,1);
}
static void mutex_unlock(struct mutex *m){CHECK(m->held);m->held=0;CHECK(pthread_mutex_unlock(&m->native)==0);}
static int mutex_is_locked(struct mutex *m){return m->held;}
struct kref {_Atomic int refs;};
static struct kref *snapshot_target;
static void kref_get(struct kref *r){CHECK(atomic_fetch_add(&r->refs,1)>0);}
static bool kref_get_unless_zero(struct kref *r){
 int n=atomic_load(&r->refs);while(n&&!atomic_compare_exchange_weak(&r->refs,&n,n+1)){}
 if(!n)return false;
 if(atomic_load(&pin_pause)&&r==snapshot_target){atomic_store(&snapshot_pinned,1);while(!atomic_load(&original_put_done))sched_yield();}
 return true;
}
static void kref_put(struct kref *r,void (*destroy)(struct kref *)){int n=atomic_fetch_sub(&r->refs,1);CHECK(n>0);if(n==1)destroy(r);}
struct dma_resv {int held,initialized;};
struct iosys_map {void *vaddr;bool is_iomem;};
static void iosys_map_clear(struct iosys_map *m){m->vaddr=NULL;m->is_iomem=false;}
static bool iosys_map_is_null(struct iosys_map *m){return !m->vaddr;}
struct device {int id;};
struct scatterlist {dma_addr_t dma_address;size_t dma_length;struct scatterlist *next;};
struct sg_table {struct scatterlist *sgl;unsigned int nents,orig_nents;};
#define sg_dma_address(s) ((s)->dma_address)
#define sg_dma_len(s) ((s)->dma_length)
#define for_each_sg(first,sg,n,i) for(i=0,sg=(first);i<(n);i++,sg=sg->next)
#define check_mul_overflow(a,b,p) __builtin_mul_overflow(a,b,p)
struct dma_buf_ops {int (*begin_cpu_access)(void);};
struct dma_buf {void *priv;const struct dma_buf_ops *ops;size_t size;struct dma_resv *resv;int refs;};
struct dma_buf_attachment {struct dma_buf *dmabuf;struct device *dev;unsigned long dma_map_attrs;};
struct drm_device;struct drm_gem_object;struct msm_gem_address_space;
struct drm_gem_object_funcs {int (*vmap)(struct drm_gem_object *,struct iosys_map *);void (*vunmap)(struct drm_gem_object *,struct iosys_map *);};
struct drm_gem_object {struct drm_device *dev;size_t size;struct dma_buf_attachment *import_attach;struct dma_resv *resv,_resv;const struct drm_gem_object_funcs *funcs;union {_Atomic int refs;struct kref refcount;};struct dma_buf *dma_buf;};
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
struct msm_gem_address_space {struct kref kref;struct mutex list_lock,cb_lock;bool domain_attached;struct msm_mmu *mmu;struct aspace_ops *ops;struct drm_mm mm;struct list_head active_list,clients;};
struct aspace_client {struct list_head list;void (*cb)(void *,bool);void *cb_data;};
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
static void *kcalloc(size_t n,size_t s,int f){if(s&&n>SIZE_MAX/s)return NULL;return kzalloc(n*s,f);}
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
static void active_add(struct msm_gem_address_space *a,struct msm_gem_object *o){CHECK(a->list_lock.held);list_add_tail(&o->iova_list,&a->active_list);o->in_active_list=true;}
static void msm_gem_add_obj_to_aspace_active_list(struct msm_gem_address_space *a,struct drm_gem_object *o){active_add(a,to_msm_bo(o));}
static void msm_gem_remove_obj_from_aspace_active_list(struct msm_gem_address_space *a,struct drm_gem_object *o){CHECK(a->list_lock.held);list_del(&to_msm_bo(o)->iova_list);to_msm_bo(o)->in_active_list=false;}
static bool is_active(struct msm_gem_object *o){(void)o;return false;}
static bool is_vunmapable(struct msm_gem_object *o){return !o->vmap_count&&o->vaddr;}
static struct page **get_pages(struct drm_gem_object *o){return to_msm_bo(o)->pages;}
static void put_pages(struct drm_gem_object *o){(void)o;}
static void *vmap(struct page **p,size_t n,int f,int prot){(void)p;(void)n;(void)f;(void)prot;return cpu_memory;}
static void vunmap(void *v){CHECK(v==cpu_memory);}
static int pgprot_writecombine(int p){return p;}
static int mmu_map(struct msm_mmu *m,struct sg_table *s,int d,unsigned int f){CHECK(m&&s&&d==0);(void)f;mmu_maps++;return mmu_error;}
static void mmu_unmap(struct msm_mmu *m,struct sg_table *s,int d,unsigned int f){CHECK(m&&s&&d==0);(void)f;mmu_unmaps++;}
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


static int update(struct msm_gem_address_space *a,bool detach){
#ifdef UPDATE_RETURNS_INT
 return msm_gem_aspace_domain_attach_detach_update(a,detach);
#else
 msm_gem_aspace_domain_attach_detach_update(a,detach);return 0;
#endif
}
struct fixture {struct kms_funcs funcs;struct msm_kms kms;struct msm_drm_private priv;struct driver driver;struct drm_device dev;struct dma_resv resv[2];struct scatterlist sg;struct sg_table sgt;struct dma_buf buf[2];struct mmu_funcs mf;struct msm_mmu mmu;struct aspace_ops ops;struct msm_gem_address_space aspace;struct drm_gem_object *obj[2];int count;};
static void setup(struct fixture *f,int count){
 memset(f,0,sizeof(*f));f->count=count;f->funcs=(struct kms_funcs){select_device,select_aspace};f->kms.funcs=&f->funcs;f->priv.kms=&f->kms;mutex_init(&f->priv.gem_lock);INIT_LIST_HEAD(&f->priv.inactive_list);
 f->driver.gem_prime_import_sg_table=msm_gem_prime_import_sg_table;f->dev=(struct drm_device){.dev=&attach_device,.driver=&f->driver,.dev_private=&f->priv};
 f->sg=(struct scatterlist){0x20000,4096,NULL};f->sgt=(struct sg_table){&f->sg,1,1};mapped_sg=&f->sgt;
 f->mf=(struct mmu_funcs){mmu_map,mmu_unmap,secure,NULL,mmu_destroy};f->mmu=(struct msm_mmu){&f->mf,&attach_device,false};f->ops.add_to_active=active_add;
 f->aspace.kref.refs=1;f->aspace.domain_attached=true;f->aspace.mmu=&f->mmu;f->aspace.ops=&f->ops;
 mutex_init(&f->aspace.list_lock);f->aspace.list_lock.which=2;mutex_init(&f->aspace.cb_lock);f->aspace.cb_lock.which=4;INIT_LIST_HEAD(&f->aspace.active_list);INIT_LIST_HEAD(&f->aspace.clients);kms_aspace=&f->aspace;
 reset();for(int i=0;i<count;i++){f->buf[i]=(struct dma_buf){.ops=&foreign_ops,.size=4096,.resv=&f->resv[i],.refs=1};f->obj[i]=msm_gem_prime_import(&f->dev,&f->buf[i]);CHECK(!IS_ERR(f->obj[i]));to_msm_bo(f->obj[i])->lock.which=1;uint64_t addr;CHECK(!msm_gem_get_iova(f->obj[i],&f->aspace,&addr));}
}
static void cleanup(struct fixture *f){for(int i=0;i<f->count;i++)if(f->obj[i])drm_gem_object_put(f->obj[i]);CHECK(!live_alloc&&!live_maps&&!live_attach);CHECK(f->aspace.kref.refs==1&&f->aspace.domain_attached);}
static int update_rc,callback_calls;
static void *detach_thread(void *p){actor=1;update_rc=update(p,true);return NULL;}
static void *free_thread(void *p){actor=2;drm_gem_object_put(p);atomic_store(&original_put_done,1);return NULL;}
static void callback(void *p,bool detach){struct fixture *f=p;callback_calls++;CHECK(!f->aspace.list_lock.held);if(atomic_load(&pin_pause)==2){atomic_store(&callback_entered,1);while(!atomic_load(&callback_release))sched_yield();}uint64_t iova;CHECK(msm_gem_get_iova(f->obj[0],&f->aspace,&iova)==0);(void)detach;}
static void *unregister_thread(void *p){struct fixture *f=p;atomic_store(&unregister_started,1);CHECK(smmu_aspace_unregister_cb(&f->aspace,callback,f)==0);atomic_store(&unregister_done,1);return NULL;}
int main(int argc,char **argv){
 (void)other_device;const char *mode=argc>1?argv[1]:"serial";struct fixture f;
 if(!strcmp(mode,"empty")){setup(&f,0);CHECK(update(&f.aspace,true)==0);CHECK(update(&f.aspace,false)==0);cleanup(&f);}
 else if(!strcmp(mode,"serial")){setup(&f,1);CHECK(update(&f.aspace,true)==0);CHECK(to_msm_bo(f.obj[0])->obj_dirty);cleanup(&f);}
 else if(!strcmp(mode,"race-zero")){setup(&f,1);atomic_store(&race_enabled,1);pthread_t a,b;CHECK(!pthread_create(&a,NULL,detach_thread,&f.aspace));while(!atomic_load(&list_held))sched_yield();CHECK(!pthread_create(&b,NULL,free_thread,f.obj[0]));pthread_join(a,NULL);pthread_join(b,NULL);CHECK(update_rc==-EBUSY);f.obj[0]=NULL;cleanup(&f);}
 else if(!strcmp(mode,"pinned-final-put")){setup(&f,1);snapshot_target=&f.obj[0]->refcount;atomic_store(&pin_pause,1);pthread_t a,b;CHECK(!pthread_create(&a,NULL,detach_thread,&f.aspace));while(!atomic_load(&snapshot_pinned))sched_yield();CHECK(!pthread_create(&b,NULL,free_thread,f.obj[0]));pthread_join(a,NULL);pthread_join(b,NULL);CHECK(update_rc==0);f.obj[0]=NULL;cleanup(&f);}
 else if(!strcmp(mode,"partial-final-put")){setup(&f,2);f.obj[1]->refs=0;snapshot_target=&f.obj[0]->refcount;atomic_store(&pin_pause,1);pthread_t a,b;CHECK(!pthread_create(&a,NULL,detach_thread,&f.aspace));while(!atomic_load(&snapshot_pinned))sched_yield();CHECK(!pthread_create(&b,NULL,free_thread,f.obj[0]));pthread_join(a,NULL);pthread_join(b,NULL);CHECK(update_rc==-EBUSY);f.obj[0]=NULL;f.obj[1]->refs=1;cleanup(&f);}
 else if(!strcmp(mode,"alloc-fail")){setup(&f,2);alloc_fail=allocations+1;CHECK(update(&f.aspace,true)==-ENOMEM);CHECK(!to_msm_bo(f.obj[0])->obj_dirty&&!to_msm_bo(f.obj[1])->obj_dirty);CHECK(f.obj[0]->refs==1&&f.obj[1]->refs==1&&!callback_calls);cleanup(&f);}
 else if(!strcmp(mode,"zero-second")){setup(&f,2);f.obj[1]->refs=0;CHECK(update(&f.aspace,true)==-EBUSY);CHECK(f.obj[0]->refs==1&&!to_msm_bo(f.obj[0])->obj_dirty&&!callback_calls);f.obj[1]->refs=1;cleanup(&f);}
 else if(!strcmp(mode,"attach-error")){setup(&f,2);CHECK(update(&f.aspace,true)==0);CHECK(update(&f.aspace,false)==-EOPNOTSUPP);CHECK(f.obj[0]->refs==1&&f.obj[1]->refs==1);cleanup(&f);}
 else if(!strcmp(mode,"callback-object")){setup(&f,1);CHECK(!smmu_aspace_register_cb(&f.aspace,callback,&f));CHECK(smmu_aspace_register_cb(&f.aspace,callback,&f)==-EEXIST);CHECK(update(&f.aspace,true)==0&&callback_calls==1);CHECK(!smmu_aspace_unregister_cb(&f.aspace,callback,&f));CHECK(smmu_aspace_unregister_cb(&f.aspace,callback,&f)==-ENOENT);cleanup(&f);}
 else if(!strcmp(mode,"callback-unregister")){setup(&f,1);CHECK(!smmu_aspace_register_cb(&f.aspace,callback,&f));atomic_store(&pin_pause,2);pthread_t a,b;CHECK(!pthread_create(&a,NULL,detach_thread,&f.aspace));while(!atomic_load(&callback_entered))sched_yield();CHECK(!pthread_create(&b,NULL,unregister_thread,&f));while(!atomic_load(&unregister_started))sched_yield();struct timespec pause={0,20000000};nanosleep(&pause,NULL);CHECK(!atomic_load(&unregister_done));atomic_store(&callback_release,1);pthread_join(a,NULL);pthread_join(b,NULL);CHECK(update_rc==0&&callback_calls==1&&atomic_load(&unregister_done));cleanup(&f);}
 else if(!strcmp(mode,"register-alloc-fail")){setup(&f,1);alloc_fail=allocations+1;CHECK(smmu_aspace_register_cb(&f.aspace,callback,&f)==-ENOMEM);cleanup(&f);}
 else if(!strcmp(mode,"attach-success")){setup(&f,2);CHECK(update(&f.aspace,false)==0);CHECK(f.obj[0]->refs==1&&f.obj[1]->refs==1);cleanup(&f);}
 else if(!strncmp(mode,"map-",4)){bool imported=!strcmp(mode,"map-short-import");setup(&f,imported?1:0);struct msm_gem_vma v={0};struct scatterlist second={0x21000,4096,NULL};int pages=1,expected=0;unsigned int flags=imported?MSM_BO_EXTBUF:0;
  if(!strcmp(mode,"map-short-local")||imported){pages=2;expected=-ERANGE;}
  else if(!strcmp(mode,"map-zero-length")){f.sg.dma_length=0;expected=-ERANGE;}
  else if(!strcmp(mode,"map-DMA0")){f.sg.dma_address=0;}
  else if(!strcmp(mode,"map-overflow")){f.sg.dma_address=UINT64_MAX-2047;expected=-ERANGE;}
  else if(!strcmp(mode,"map-discontiguous")){f.sg.next=&second;f.sgt.nents=f.sgt.orig_nents=2;second.dma_address=0x22000;pages=2;expected=-ERANGE;}
  else if(!strcmp(mode,"map-contiguous")){f.sg.next=&second;f.sgt.nents=f.sgt.orig_nents=2;pages=2;}
  else if(!strcmp(mode,"map-provider-error")){mmu_error=-EIO;expected=-EIO;}
  else CHECK(false);
  int refs=f.aspace.kref.refs,unmap_before=mmu_unmaps;CHECK(smmu_aspace_map_vma(&f.aspace,&v,&f.sgt,pages,flags)==expected);
  if(expected){CHECK(!v.mapped&&!v.iova&&f.aspace.kref.refs==refs);CHECK(mmu_unmaps==unmap_before+(expected==-ERANGE));}
  else{CHECK(v.mapped&&v.iova==f.sg.dma_address&&f.aspace.kref.refs==refs+1);smmu_aspace_unmap_vma(&f.aspace,&v,&f.sgt,flags);CHECK(!v.mapped&&!v.iova&&f.aspace.kref.refs==refs);}
  if(imported){CHECK(live_maps==1&&live_attach==1&&!unmaps&&!detaches);}cleanup(&f);}
 else if(!strcmp(mode,"null")){CHECK(update(NULL,true)==-EINVAL);}
 else {fprintf(stderr,"unknown mode %s\n",mode);return 2;}
 printf("PASS %s: actual source update/free/get_iova/client operations; pthread/ref/DMA leaves modeled\n",mode);return 0;
}
