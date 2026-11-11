/* SPDX-License-Identifier: GPL-2.0-only */
/* Actual owner/GEM/PRIME/close bodies; device, DMA, locks and managed core modeled. */
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
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
struct mutex {pthread_mutex_t m;atomic_int held;};
static void mutex_init(struct mutex*m){CHECK(!pthread_mutex_init(&m->m,NULL));atomic_init(&m->held,0);}
static void mutex_lock(struct mutex*m){CHECK(!pthread_mutex_lock(&m->m));atomic_store(&m->held,1);}
static void mutex_unlock(struct mutex*m){CHECK(atomic_load(&m->held));atomic_store(&m->held,0);CHECK(!pthread_mutex_unlock(&m->m));}
static int mutex_is_locked(struct mutex*m){return atomic_load(&m->held);}
typedef struct {atomic_int value;}atomic_t;
static void atomic_set(atomic_t*a,int n){atomic_init(&a->value,n);}
static void atomic_inc(atomic_t*a){atomic_fetch_add(&a->value,1);}
static int atomic_read(atomic_t*a){return atomic_load(&a->value);}
static bool atomic_dec_and_test(atomic_t*a){return atomic_fetch_sub(&a->value,1)==1;}
typedef struct {pthread_mutex_t m;pthread_cond_t cv;}wait_queue_head_t;
static void init_waitqueue_head(wait_queue_head_t*w){CHECK(!pthread_mutex_init(&w->m,NULL));CHECK(!pthread_cond_init(&w->cv,NULL));}
static void wake_up_all(wait_queue_head_t*w){pthread_mutex_lock(&w->m);pthread_cond_broadcast(&w->cv);pthread_mutex_unlock(&w->m);}
#define wait_event(w,c) do{pthread_mutex_lock(&(w).m);while(!(c))pthread_cond_wait(&(w).cv,&(w).m);pthread_mutex_unlock(&(w).m);}while(0)
static bool list_empty(struct list_head*h){return h->next==h;}
#define list_del_init list_del
struct drm_mm {int unused;};
struct drm_mm_node {unsigned long start;};
struct msm_file_private;
struct msm_vm_client_entry {struct list_head list;};
struct msm_drm_thread {void*thread;int worker;};
struct kref {int refs;};
static void kref_get(struct kref *r){CHECK(r->refs>0);r->refs++;}
static bool kref_get_unless_zero(struct kref *r){if(!r->refs)return false;kref_get(r);return true;}
static void kref_put(struct kref *r,void (*destroy)(struct kref *)){CHECK(r->refs>0);if(!--r->refs)destroy(r);}
struct dma_resv {int held,initialized;};
struct iosys_map {void *vaddr;bool is_iomem;};
static void iosys_map_clear(struct iosys_map *m){m->vaddr=NULL;m->is_iomem=false;}
static bool iosys_map_is_null(struct iosys_map *m){return !m->vaddr;}
struct platform_device;struct device {int id;struct platform_device*pdev;};
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
struct drm_file;
struct msm_kms;struct kms_funcs {struct device *(*get_address_space_device)(struct msm_kms *,u32);struct msm_gem_address_space *(*get_address_space)(struct msm_kms *,u32);void(*debugfs_destroy)(struct msm_kms*);void(*destroy)(struct msm_kms*);void(*postopen)(struct msm_kms*,struct drm_file*);void(*preclose)(struct msm_kms*,struct drm_file*);void(*postclose)(struct msm_kms*,struct drm_file*);};
struct msm_kms {const struct kms_funcs *funcs;};
struct msm_drm_private {struct msm_kms*kms;struct mutex gem_lock;struct list_head inactive_list;
 struct drm_device*dev;struct mutex lifetime_lock;wait_queue_head_t lifetime_wait;atomic_t active_users;bool retired,vram_initialized,vram_allocated;
 void*wq;int num_crtcs;struct msm_drm_thread disp_thread[2],event_thread[2];void*pp_event_thread;int pp_event_worker;void*debug_root;bool registered;
 struct {unsigned long size;dma_addr_t paddr;struct drm_mm mm;int lock;}vram;
 struct mutex vm_client_lock;struct list_head vm_client_list;int phandle;struct msm_file_private*lastctx;
};
struct msm_file_private {struct msm_drm_private*owner;struct mutex power_lock;unsigned short enable_refcnt;};
struct drm_device {struct device *dev;struct driver *driver;void *dev_private;struct mutex struct_mutex,object_name_lock;atomic_int refs;void*managed_priv;void(*managed_action)(struct drm_device*,void*);};
struct platform_device {struct device dev;struct drm_device*drvdata;};
#define to_platform_device(d) ((d)->pdev)
static struct drm_device*platform_get_drvdata(struct platform_device*p){return p->drvdata;}
static void platform_set_drvdata(struct platform_device*p,struct drm_device*d){p->drvdata=d;}
#define drm_driver driver
struct page {int unused;};
struct msm_mmu;struct mmu_funcs {int (*map_dma_buf)(struct msm_mmu *,struct sg_table *,int,unsigned int);void (*unmap_dma_buf)(struct msm_mmu *,struct sg_table *,int,unsigned int);bool (*is_domain_secure)(struct msm_mmu *);struct device *(*get_dev)(struct msm_mmu *);void (*destroy)(struct msm_mmu *);};
struct msm_mmu {struct mmu_funcs *funcs;struct device *dev;bool secure;};
struct msm_gem_object;
struct aspace_ops {void (*add_to_active)(struct msm_gem_address_space *,struct msm_gem_object *);};
struct msm_gem_address_space {struct kref kref;struct mutex list_lock;bool domain_attached;struct msm_mmu *mmu;struct aspace_ops *ops;struct drm_mm mm;};
struct msm_gem_vma {struct list_head list;struct msm_gem_address_space *aspace;uint64_t iova;bool mapped;struct drm_mm_node node;};
struct msm_gem_object {struct drm_gem_object base;struct msm_drm_private*owner;struct drm_mm_node*vram_node;u32 flags;u8 madv,vmap_count;struct mutex lock;struct list_head mm_list,submit_entry,vmas,iova_list;struct msm_gem_address_space *aspace,*import_aspace;bool in_active_list,obj_dirty,aspace_ref;struct dma_resv *resv,_resv;struct sg_table *sgt;struct page **pages;void *vaddr;struct iosys_map import_map;int import_error;};
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
static atomic_int allocations,live_alloc;
static int alloc_fail,attach_error,map_error,live_attach,live_maps,ref_inits,ref_finis;
static int maps,unmaps,attaches,detaches,mmu_maps,mmu_unmaps,mmu_error;
static int cpu_begin_error,cpu_end_error,cpu_map_error,cpu_begins,cpu_ends,cpu_maps,cpu_unmaps;
static bool iomem_map;
static bool domain_alive=true;
static int domain_destroys;
static struct msm_gem_address_space *kms_aspace;
static struct sg_table *mapped_sg;
static struct device attach_device={.id=1},other_device={.id=2};
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
static void block_attachment_map(void);
static struct sg_table *dma_buf_map_attachment(struct dma_buf_attachment *a,int d){CHECK(a&&d==DMA_BIDIRECTIONAL);block_attachment_map();maps++;if(map_error)return ERR_PTR(map_error);live_maps++;return mapped_sg;}
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
static int early_publish;
static void drm_gem_private_object_init(struct drm_device *d,struct drm_gem_object *o,size_t n){if(to_msm_bo(o)->mm_list.next!=&to_msm_bo(o)->mm_list)early_publish++;o->dev=d;o->size=n;o->refs=1;dma_resv_init(&o->_resv);if(!o->resv)o->resv=&o->_resv;}
static void drm_gem_object_release(struct drm_gem_object *o){dma_resv_fini(&o->_resv);}
static void drm_gem_object_get(struct drm_gem_object *o){CHECK(o->refs>0);o->refs++;}
static void msm_gem_free_object(struct drm_gem_object *o);
static void drm_gem_object_put_unlocked(struct drm_gem_object *o){if(o&&!--o->refs)msm_gem_free_object(o);}
static void drm_gem_object_put(struct drm_gem_object *o){drm_gem_object_put_unlocked(o);}
void msm_gem_address_space_put(struct msm_gem_address_space *);
static void drm_mm_takedown(struct drm_mm *m);
static void mmu_destroy(struct msm_mmu *m){CHECK(m&&domain_alive&&!live_maps&&!live_attach);domain_alive=false;domain_destroys++;}
static struct device *msm_gem_get_aspace_device(struct msm_gem_address_space *a){return a&&a->mmu?a->mmu->dev:NULL;}
static void active_add(struct msm_gem_address_space *a,struct msm_gem_object *o){CHECK(a->list_lock.held);o->in_active_list=true;}
static void msm_gem_add_obj_to_aspace_active_list(struct msm_gem_address_space *a,struct drm_gem_object *o){active_add(a,to_msm_bo(o));}
static void msm_gem_remove_obj_from_aspace_active_list(struct msm_gem_address_space *a,struct drm_gem_object *o){CHECK(a->list_lock.held);to_msm_bo(o)->in_active_list=false;}
static bool is_active(struct msm_gem_object *o){(void)o;return false;}
static bool is_vunmapable(struct msm_gem_object *o){return !o->vmap_count&&o->vaddr;}
static struct page **get_pages(struct drm_gem_object *o){return to_msm_bo(o)->pages;}
static void put_pages(struct drm_gem_object *o);
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
struct drm_file {struct drm_prime_file_private prime;void*driver_priv;};
static struct drm_gem_object *lookup_object;
static struct drm_gem_object *drm_gem_object_lookup(struct drm_file *f,uint32_t handle){CHECK(f->prime.lock.held&&handle==1);drm_gem_object_get(lookup_object);return lookup_object;}
static struct dma_buf *drm_prime_lookup_buf_by_handle(struct drm_prime_file_private *p,uint32_t handle){CHECK(p->lock.held&&handle==1);return p->cached;}
static int drm_prime_add_buf_handle(struct drm_prime_file_private *p,struct dma_buf *b,uint32_t handle){CHECK(p->lock.held&&handle==1&&!p->cached);get_dma_buf(b);p->cached=b;return 0;}
static struct dma_buf *export_and_register_object(struct drm_device *d,struct drm_gem_object *o,uint32_t flags){(void)d;(void)o;(void)flags;CHECK(false);return ERR_PTR(-EINVAL);}
#endif

#include "sde_owner_lifetime_support.h"
#include "actual-functions.h"
#include "sde_owner_lifetime_cases.h"
