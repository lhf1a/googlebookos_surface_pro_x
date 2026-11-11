/* Production lifecycle functions. Domain, io-pgtable, SCM and PM execution modeled. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
typedef uint64_t u64;typedef uint32_t u32;
#define EPROBE_DEFER 517
#define PAGE_SIZE 4096UL
#define PAGE_SHIFT 12
#define KGSL_IOMMU_GLOBAL_MEM_SIZE (20UL*1024*1024)
#define KGSL_GLOBAL_MEM_PAGES (KGSL_IOMMU_GLOBAL_MEM_SIZE>>PAGE_SHIFT)
#define KGSL_IOMMU_SPLIT_TABLE_BASE 0x0001ff8000000000ULL
#define KGSL_MEMDESC_GMU_MAPPED (1U << 11)
#define KGSL_MEMDESC_RANDOM 1
#define KGSL_MEMDESC_MAPPED 2
#define KGSL_MEMDESC_MAP_UNCERTAIN 4
#define KGSL_MMU_64BIT 0
#define KGSL_MMU_GLOBAL_PAGETABLE 1
#define KGSL_MMU_PAGED 2
#define KGSL_MMU_SPLIT_TABLES_GC 3
#define KGSL_MMU_GLOBAL_PT 0
#define KGSL_IOMMU_SMMU_V500 1
#define KGSL_MMU_TYPE_IOMMU 1
#define IO_PGTABLE_QUIRK_ARM_TTBR1 32
#define GFP_KERNEL 0
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p)>=(uintptr_t)-4095)
#define IS_ERR_OR_NULL(p) (!(p)||IS_ERR(p))
#define PTR_ERR_OR_ZERO(p) (IS_ERR(p)?PTR_ERR(p):0)
#define IS_ERR_VALUE(x) ((u64)(x)>=(u64)-4095)
#define IS_ALIGNED(v,a) (!((v)&((a)-1)))
#define BITS_TO_LONGS(n) (((n)+63)/64)
#define dev_err(...) ((void)0)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
struct list_head {struct list_head *next,*prev;};
static void INIT_LIST_HEAD(struct list_head *h){h->next=h->prev=h;}
static void list_add_tail(struct list_head *n,struct list_head *h){n->prev=h->prev;n->next=h;h->prev->next=n;h->prev=n;}
static void list_add(struct list_head *n,struct list_head *h){list_add_tail(n,h);}
static void list_del(struct list_head *n){n->prev->next=n->next;n->next->prev=n->prev;INIT_LIST_HEAD(n);}
#define list_for_each_entry(p,h,m) for(struct list_head *it=(h)->next;it!=(h)&&((p)=container_of(it,__typeof__(*(p)),m),1);it=it->next)
#define list_for_each_entry_safe(p,tmp,h,m) for(struct list_head *it=(h)->next,*next=it->next;it!=(h)&&((p)=container_of(it,__typeof__(*(p)),m),(tmp)=NULL,(void)(tmp),1);it=next,next=it->next)
struct device_node {int refs;};
struct device {int refs,pm;struct device_node *of_node;void *data;};
struct platform_device {struct device dev;};
struct kgsl_device;struct kgsl_pagetable;
struct io_pgtable_cfg {struct device *iommu_dev;unsigned quirks,ias;u64 root;};
struct adreno_smmu_priv {const void *cookie;const struct io_pgtable_cfg *(*get_ttbr1_cfg)(const void *);int (*get_context_bank)(const void *,u32 *,u32 *);int (*set_ttbr0_cfg)(const void *,const struct io_pgtable_cfg *);int (*reset_ttbr0)(const void *);int (*flush_tlb)(const void *);void (*get_fault_info)(void);int (*set_fault_policy)(void);int (*terminate_fault)(const void *);int (*rearm_fault)(const void *);};
struct iommu_domain {bool attached;};
struct kgsl_iommu_table {struct io_pgtable_cfg cfg;struct kgsl_pagetable *parent;u64 ttbr0;};
struct kgsl_iommu_pt {struct iommu_domain *domain;struct kgsl_iommu_table *bootstrap;bool bootstrap_enabled,attached,poisoned;u64 ttbr0;u32 contextidr;};
struct kgsl_iommu_context {struct kgsl_pagetable *default_pt;int cb_num;const char *name;struct kgsl_device *kgsldev;};
struct kgsl_iommu {struct device *smmu_dev;struct kgsl_pagetable *failed_parent;bool pm_held,started;struct kgsl_iommu_context user_context;};
struct kgsl_mmu;
struct kgsl_mmu_ops {int (*mmu_init_pt)(struct kgsl_mmu *,struct kgsl_pagetable *);};
struct kgsl_mmu {struct kgsl_device *device;unsigned long features;bool secured;int subtype,type;struct kgsl_iommu iommu;struct kgsl_pagetable *defaultpagetable;const struct kgsl_mmu_ops *mmu_ops;};
struct kgsl_device {struct platform_device *pdev;struct device *dev;struct kgsl_mmu mmu;struct adreno_smmu_priv smmu;struct list_head globals;unsigned long *global_map;void *qdss_desc,*qtimer_desc;};
struct kref {int refcount;};
struct kgsl_pagetable {struct device *retained_dev;struct kref refcount;void *priv,*pt_ops;int lock,destroy_ws,destroy_error;unsigned name;struct kgsl_mmu *mmu;struct list_head list;struct {int entries,mapped,max_mapped;}stats;};
struct sg_table {int unused;};
struct kgsl_memdesc {struct sg_table *sgt;u64 size,gpuaddr;u32 priv;bool secure;struct kgsl_pagetable *pagetable;};
struct kgsl_global_memdesc {struct list_head node;struct kgsl_memdesc memdesc;const char *name;};
#define KGSL_MMU_DEVICE(m) ((m)->device)
#define _IOMMU_PRIV(m) (&(m)->iommu)
#define KGSL_IOMMU_PRIV(d) (&(d)->mmu.iommu)
#define MMU_OP_VALID(m,op) ((m)->mmu_ops&&(m)->mmu_ops->op)
static struct {int ptlock;struct list_head pagetable_list;}kgsl_driver;
static struct list_head retained_pagetables;
static int module_refs;
#define THIS_MODULE (&module_refs)
static void __module_get(int *p){(*p)++;}
#define spin_lock_irqsave(p,f) do{(f)=0;}while(0)
#define spin_unlock_irqrestore(p,f) ((void)(f))
#define spin_lock_init(p) (*(p)=0)
#define INIT_WORK(p,fn) (*(p)=0)
#define atomic_set(p,v) (*(p)=(v))
#define atomic_long_set(p,v) (*(p)=(v))
static void kref_init(struct kref *p){p->refcount=1;}
static int refcount_read(int *p){return *p;}
static bool test_bit(int b,unsigned long *p){return !!(*p&(1UL<<b));}
static void set_bit(int b,unsigned long *p){*p|=1UL<<b;}
static bool kgsl_iommu_split_tables_enabled(struct kgsl_mmu *m){return test_bit(KGSL_MMU_SPLIT_TABLES_GC,&m->features);}
static bool kgsl_memdesc_is_secured(struct kgsl_memdesc *m){return m->secure;}
static u64 kgsl_memdesc_footprint(struct kgsl_memdesc *m){return m->size;}
static struct kgsl_device gpu;static struct platform_device gpu_platform,smmu_platform;static struct device character_device;static struct device_node gpu_node,smmu_node;
static struct io_pgtable_cfg cfg;static struct iommu_domain *current_domain;
static int domain_count,table_count,cache_count,metadata_error,domain_error,attach_error,bank_error,scm_error,table_error,install_error,disable_error,destroy_error,flush_error,pm_error,terminate_error,rearm_error,map_error,unmap_error;
static bool compatible,mapped,node_present,supplier_present,interface_missing,detach_failure,enabled,wrong_cfg_device,uncertain;
static int install_calls,disable_calls,flush_calls,detach_calls,domain_frees,table_frees,shared_frees;
static void *addr_entry_cache;static void *kgsl_guard_page;
static unsigned bank=3;
static struct device *get_device(struct device *d){d->refs++;return d;}
static void put_device(struct device *d){assert(d->refs>1);d->refs--;}
static int pm_runtime_resume_and_get(struct device *d){assert(d==&smmu_platform.dev);if(pm_error)return pm_error;d->pm++;return 0;}
static void pm_runtime_put_autosuspend(struct device *d){assert(d==&smmu_platform.dev&&d->pm>0);d->pm--;}
static void *kzalloc(size_t size,int gfp){return calloc(1,size);}
static void *kcalloc(size_t n,size_t size,int gfp){return calloc(n,size);}
#define kfree free
static void *__cache_alloc(void){cache_count++;return (void *)0x555;}
#define KMEM_CACHE(t,f) __cache_alloc()
static void kmem_cache_destroy(void *p){assert(p==(void *)0x555&&cache_count==1);cache_count--;}
static void __free_page(void *p){free(p);}
static bool device_iommu_mapped(struct device *d){assert(d==&gpu_platform.dev);return mapped;}
static void *dev_get_drvdata(struct device *d){return d->data;}
static struct device_node *of_parse_phandle(struct device_node *n,const char *s,int i){assert(n==&gpu_node&&!strcmp(s,"iommus")&&!i);if(!node_present)return NULL;smmu_node.refs++;return &smmu_node;}
static bool of_device_is_compatible(struct device_node *n,const char *s){assert(n==&smmu_node);return compatible;}
static void of_node_put(struct device_node *n){assert(n==&smmu_node&&n->refs==1);n->refs--;}
static struct platform_device *of_find_device_by_node(struct device_node *n){assert(n==&smmu_node);if(!supplier_present)return NULL;get_device(&smmu_platform.dev);return &smmu_platform;}
static void assert_pm(void){assert(smmu_platform.dev.pm==1);}
static const struct io_pgtable_cfg *get_cfg(const void *cookie){assert(cookie==current_domain);cfg.iommu_dev=wrong_cfg_device?&character_device:&smmu_platform.dev;return &cfg;}
static int get_bank(const void *cookie,u32 *cb,u32 *asid){assert(cookie==current_domain);*cb=bank;*asid=0x123;return bank_error;}
static int set_root(const void *cookie,const struct io_pgtable_cfg *c){assert_pm();assert(cookie==current_domain);if(c){install_calls++;assert(!enabled&&table_count==1&&c->root==0x9000);if(install_error)return install_error;enabled=true;}else{disable_calls++;assert(enabled);if(disable_error)return disable_error;enabled=false;}return 0;}
static int reset_root(const void *cookie){assert_pm();assert(enabled);return 0;}
static int flush(const void *cookie){assert_pm();assert(cookie==current_domain);flush_calls++;return flush_error;}
static void get_fault(void){}
static int set_policy(void){return 0;}
static int terminate(const void *cookie){assert_pm();return terminate_error;}
static int rearm(const void *cookie){assert_pm();return rearm_error;}
static struct kgsl_iommu_pt *_alloc_pt_metadata(struct kgsl_mmu *m,struct kgsl_pagetable *p){if(metadata_error)return ERR_PTR(metadata_error);p->priv=calloc(1,sizeof(struct kgsl_iommu_pt));p->pt_ops=(void *)1;return p->priv;}
static struct iommu_domain *iommu_paging_domain_alloc(struct device *d){assert_pm();assert(d==&gpu_platform.dev&&d!=gpu.dev);if(domain_error)return ERR_PTR(domain_error);domain_count++;return calloc(1,sizeof(struct iommu_domain));}
static int iommu_attach_device(struct iommu_domain *d,struct device *dev){assert_pm();assert(dev==&gpu_platform.dev);gpu.smmu=(struct adreno_smmu_priv){d,get_cfg,get_bank,set_root,reset_root,flush,get_fault,set_policy,terminate,rearm};if(attach_error)return attach_error;d->attached=true;current_domain=d;if(interface_missing)gpu.smmu.flush_tlb=NULL;return 0;}
static void iommu_detach_device(struct iommu_domain *d,struct device *dev){assert_pm();assert(dev==&gpu_platform.dev&&current_domain==d&&!enabled);detach_calls++;if(!detach_failure){d->attached=false;current_domain=NULL;}}
static struct iommu_domain *iommu_get_domain_for_dev(struct device *d){assert(d==&gpu_platform.dev);return current_domain;}
static void iommu_domain_free(struct iommu_domain *d){assert_pm();assert(!d->attached&&!enabled&&!table_count);domain_count--;domain_frees++;free(d);}
static int kgsl_iommu_fault_handler(void){return 0;}
static void iommu_set_fault_handler(struct iommu_domain *d,int (*fn)(void),struct kgsl_pagetable *p){assert(enabled&&p->priv&&d==current_domain&&fn==kgsl_iommu_fault_handler);}
static int set_smmu_aperture(struct kgsl_device *d,int cb){assert_pm();assert(d==&gpu&&cb==bank);return scm_error;}
static struct kgsl_iommu_table *kgsl_iommu_table_create(struct kgsl_pagetable *p,struct adreno_smmu_priv *sm){assert_pm();assert(sm==&gpu.smmu);if(table_error)return ERR_PTR(table_error);struct kgsl_iommu_table *t=calloc(1,sizeof(*t));t->parent=p;t->cfg=cfg;t->cfg.root=0x9000;t->ttbr0=0x123000000009000ULL;p->refcount.refcount++;table_count++;return t;}
static int kgsl_iommu_table_destroy(struct kgsl_iommu_table *t){assert_pm();assert(!enabled);if(destroy_error)return destroy_error;t->parent->refcount.refcount--;table_count--;table_frees++;free(t);return 0;}
static int kgsl_mmu_map(struct kgsl_pagetable *p,struct kgsl_memdesc *m){assert_pm();assert(p==gpu.mmu.defaultpagetable);if(map_error){if(uncertain)m->priv|=KGSL_MEMDESC_MAP_UNCERTAIN;return map_error;}m->priv|=KGSL_MEMDESC_MAPPED;return 0;}
static int kgsl_mmu_unmap(struct kgsl_pagetable *p,struct kgsl_memdesc *m){assert_pm();assert(p==gpu.mmu.defaultpagetable);return unmap_error;}
static unsigned get_random_u32_below(unsigned bound){assert(bound);return bound-1;}
static unsigned long bitmap_find_next_zero_area(unsigned long *b,unsigned long n,unsigned long start,unsigned long len,unsigned long mask){for(unsigned long i=start;i+len<=n;i++){bool free=true;for(unsigned long j=0;j<len;j++)if(b[(i+j)/64]&(1UL<<((i+j)%64))){free=false;break;}if(free)return i;}return n;}
static void bitmap_set(unsigned long *b,unsigned long start,unsigned long n){assert(start+n<=KGSL_GLOBAL_MEM_PAGES);for(unsigned long i=start;i<start+n;i++)b[i/64]|=1UL<<(i%64);}
static void bitmap_clear(unsigned long *b,unsigned long start,unsigned long n){assert(start+n<=KGSL_GLOBAL_MEM_PAGES);for(unsigned long i=start;i<start+n;i++)b[i/64]&=~(1UL<<(i%64));}
static void pagetable_add_sysfs_objects(struct kgsl_pagetable *p){}
static void kgsl_mmu_putpagetable(struct kgsl_pagetable *p){assert(p->refcount.refcount==1&&!p->priv);list_del(&p->list);free(p);}
struct kgsl_pagetable *kgsl_mmu_createpagetableobject(struct kgsl_mmu *,unsigned);
static struct kgsl_pagetable *kgsl_mmu_getpagetable(struct kgsl_mmu *m,unsigned name){return kgsl_mmu_createpagetableobject(m,name);}
static int _init_global_pt(struct kgsl_mmu *,struct kgsl_pagetable *);
static const struct kgsl_mmu_ops kgsl_iommu_ops={_init_global_pt};
static int kgsl_mmu_map_global(struct kgsl_device *d,struct kgsl_memdesc *m,u32 padding);
static int checked_unmap_error, checked_unmap_calls;
static int arm_smmu_enable_checked_unmap(struct iommu_domain *d) { assert(d && d->attached); checked_unmap_calls++; return checked_unmap_error; }
static int sg_frees;
static void sg_free_table(struct sg_table *s){assert(s);sg_frees++;}
static int kgsl_sharedmem_free(struct kgsl_memdesc *m){if(m->priv&(KGSL_MEMDESC_MAPPED|KGSL_MEMDESC_MAP_UNCERTAIN))return -EBUSY;shared_frees++;return 0;}
#include "parent-functions.h"
static int kgsl_mmu_map_global(struct kgsl_device *d,struct kgsl_memdesc *m,u32 padding){return kgsl_iommu_map_global(&d->mmu,m,padding);}
static void setup(void){
 checked_unmap_error=checked_unmap_calls=0;
 assert(!domain_count&&!table_count&&!cache_count&&!module_refs);
 memset(&gpu,0,sizeof(gpu));memset(&gpu_platform,0,sizeof(gpu_platform));memset(&smmu_platform,0,sizeof(smmu_platform));memset(&character_device,0,sizeof(character_device));
 gpu_platform.dev.refs=smmu_platform.dev.refs=character_device.refs=1;gpu_platform.dev.of_node=&gpu_node;gpu_platform.dev.data=&gpu;gpu.pdev=&gpu_platform;gpu.dev=&character_device;gpu.mmu.device=&gpu;gpu.mmu.features=1UL<<KGSL_MMU_64BIT;
 INIT_LIST_HEAD(&gpu.globals);INIT_LIST_HEAD(&kgsl_driver.pagetable_list);INIT_LIST_HEAD(&retained_pagetables);
 cfg=(struct io_pgtable_cfg){&smmu_platform.dev,IO_PGTABLE_QUIRK_ARM_TTBR1,39,0};current_domain=NULL;addr_entry_cache=NULL;kgsl_guard_page=NULL;
 compatible=mapped=node_present=supplier_present=true;interface_missing=detach_failure=enabled=wrong_cfg_device=uncertain=false;
 metadata_error=domain_error=attach_error=bank_error=scm_error=table_error=install_error=disable_error=destroy_error=flush_error=pm_error=terminate_error=rearm_error=map_error=unmap_error=0;
 install_calls=disable_calls=flush_calls=detach_calls=domain_frees=table_frees=shared_frees=0;bank=3;
}
static void balanced(void){assert(!domain_count&&!table_count&&!cache_count&&!module_refs&&!smmu_platform.dev.pm&&smmu_platform.dev.refs==1&&character_device.refs==1&&!smmu_node.refs&&!enabled&&!current_domain);}
static void probe_ok(void){assert(!kgsl_iommu_probe(&gpu));struct kgsl_pagetable *p=gpu.mmu.defaultpagetable;struct kgsl_iommu_pt *i=p->priv;assert(p->refcount.refcount==2&&i->bootstrap_enabled&&i->bootstrap&&i->ttbr0==i->bootstrap->ttbr0&&i->contextidr==0&&gpu.mmu.iommu.user_context.cb_num==3&&gpu.mmu.iommu.pm_held&&gpu.mmu.iommu.smmu_dev==&smmu_platform.dev&&enabled&&install_calls==1);}
static void close_ok(void){assert(!kgsl_iommu_close(&gpu.mmu));kgsl_free_globals(&gpu);balanced();}
/* Test-only cleanup after failure injection; production automatic recovery is not implied. */
static void dispose_failed_fixture(void){struct kgsl_pagetable *p=gpu.mmu.iommu.failed_parent;assert(p&&p->retained_dev==&character_device&&module_refs==1&&character_device.refs==2);flush_error=destroy_error=disable_error=0;detach_failure=false;gpu.smmu.flush_tlb=flush;assert(!kgsl_iommu_release_parent(p));list_del(&p->list);free(p);module_refs--;put_device(&character_device);pm_runtime_put_autosuspend(&smmu_platform.dev);put_device(&smmu_platform.dev);kmem_cache_destroy(addr_entry_cache);balanced();}
static struct kgsl_global_memdesc *memory(u64 size){struct kgsl_global_memdesc *m=calloc(1,sizeof(*m));m->memdesc.size=size;m->name="test";list_add_tail(&m->node,&gpu.globals);return m;}
int main(void){int cases=0;
 setup();probe_ok();assert(kgsl_iommu_probe(&gpu)==-EBUSY);close_ok();assert(disable_calls==1&&detach_calls==1&&table_frees==1&&domain_frees==1);cases++;
 setup();mapped=false;assert(kgsl_iommu_probe(&gpu)==-ENODEV);balanced();cases++;
 setup();gpu_platform.dev.data=&character_device;assert(kgsl_iommu_probe(&gpu)==-ENODEV);balanced();cases++;
 setup();gpu.mmu.secured=true;assert(kgsl_iommu_probe(&gpu)==-EOPNOTSUPP);balanced();cases++;
 setup();gpu.mmu.features=0;assert(kgsl_iommu_probe(&gpu)==-EOPNOTSUPP);balanced();cases++;
 setup();gpu.mmu.features|=1UL<<KGSL_MMU_GLOBAL_PAGETABLE;assert(kgsl_iommu_probe(&gpu)==-EOPNOTSUPP);balanced();cases++;
 setup();node_present=false;assert(kgsl_iommu_probe(&gpu)==-ENODEV);balanced();cases++;
 setup();compatible=false;assert(kgsl_iommu_probe(&gpu)==-EOPNOTSUPP);balanced();cases++;
 setup();supplier_present=false;assert(kgsl_iommu_probe(&gpu)==-EPROBE_DEFER);balanced();cases++;
 setup();pm_error=-EIO;assert(kgsl_iommu_probe(&gpu)==-EIO);balanced();cases++;
 setup();metadata_error=-ENOMEM;assert(kgsl_iommu_probe(&gpu)==-ENOMEM);balanced();cases++;
 setup();domain_error=-EAGAIN;assert(kgsl_iommu_probe(&gpu)==-EAGAIN);balanced();cases++;
 setup();attach_error=-EBUSY;assert(kgsl_iommu_probe(&gpu)==-EBUSY&&!gpu.smmu.cookie);balanced();cases++;
 setup();interface_missing=true;assert(kgsl_iommu_probe(&gpu)==-ENODEV);balanced();cases++;
 setup();wrong_cfg_device=true;assert(kgsl_iommu_probe(&gpu)==-EOPNOTSUPP);balanced();cases++;
 setup();cfg.ias=38;assert(kgsl_iommu_probe(&gpu)==-EOPNOTSUPP);balanced();cases++;
 setup();bank_error=-EIO;assert(kgsl_iommu_probe(&gpu)==-EIO);balanced();cases++;
 setup();bank=32;assert(kgsl_iommu_probe(&gpu)==-EOPNOTSUPP);balanced();cases++;
 setup();scm_error=-EPROBE_DEFER;assert(kgsl_iommu_probe(&gpu)==-EPROBE_DEFER);balanced();cases++;
 setup();table_error=-ENOMEM;assert(kgsl_iommu_probe(&gpu)==-ENOMEM);balanced();cases++;
 setup();install_error=-EIO;assert(kgsl_iommu_probe(&gpu)==-EIO&&!disable_calls);balanced();cases++;
 setup();install_error=-EIO;destroy_error=-ETIMEDOUT;assert(kgsl_iommu_probe(&gpu)==-EIO);assert(domain_count==1&&table_count==1&&gpu.mmu.iommu.failed_parent->refcount.refcount==2&&smmu_platform.dev.pm==1);assert(kgsl_iommu_probe(&gpu)==-EBUSY);dispose_failed_fixture();cases++;
 setup();scm_error=-EIO;flush_error=-ETIMEDOUT;assert(kgsl_iommu_probe(&gpu)==-EIO&&domain_count==1);dispose_failed_fixture();cases++;
 setup();install_error=-EIO;detach_failure=true;assert(kgsl_iommu_probe(&gpu)==-EIO&&domain_count==1&&!table_count);dispose_failed_fixture();cases++;
 setup();probe_ok();gpu.mmu.defaultpagetable->refcount.refcount++;assert(kgsl_iommu_close(&gpu.mmu)==-EBUSY&&!disable_calls&&!detach_calls);gpu.mmu.defaultpagetable->refcount.refcount--;close_ok();cases++;
 setup();probe_ok();gpu.mmu.iommu.started=true;assert(kgsl_iommu_close(&gpu.mmu)==-EBUSY&&!disable_calls);gpu.mmu.iommu.started=false;close_ok();cases++;
 setup();probe_ok();disable_error=-EIO;assert(kgsl_iommu_close(&gpu.mmu)==-EIO&&enabled&&table_count==1);disable_error=0;close_ok();cases++;
 setup();probe_ok();destroy_error=-EIO;assert(kgsl_iommu_close(&gpu.mmu)==-EIO&&!enabled&&table_count==1&&domain_count==1);destroy_error=0;close_ok();assert(disable_calls==1);cases++;
 setup();probe_ok();terminate_error=-ETIMEDOUT;assert(kgsl_iommu_close(&gpu.mmu)==-ETIMEDOUT&&enabled&&!disable_calls);terminate_error=0;close_ok();cases++;
 setup();probe_ok();rearm_error=-EBUSY;assert(kgsl_iommu_close(&gpu.mmu)==-EBUSY&&enabled);rearm_error=0;close_ok();cases++;
 setup();probe_ok();struct kgsl_global_memdesc *m=memory(PAGE_SIZE);assert(!kgsl_map_global_allocation(&gpu,m,0)&&m->memdesc.gpuaddr==KGSL_IOMMU_SPLIT_TABLE_BASE&&m->memdesc.pagetable==gpu.mmu.defaultpagetable);close_ok();assert(shared_frees==1);cases++;
 setup();probe_ok();m=memory(PAGE_SIZE);map_error=-ENOMEM;assert(kgsl_map_global_allocation(&gpu,m,0)==-ENOMEM&&shared_frees==1&&gpu.globals.next==&gpu.globals);map_error=0;close_ok();cases++;
 setup();probe_ok();m=memory(PAGE_SIZE);map_error=-EUCLEAN;uncertain=true;assert(kgsl_map_global_allocation(&gpu,m,0)==-EUCLEAN&&!shared_frees&&m->memdesc.gpuaddr);assert(kgsl_iommu_close(&gpu.mmu)==-EUCLEAN&&enabled);kgsl_free_globals(&gpu);assert(!shared_frees&&gpu.globals.next==&m->node);m->memdesc.priv=0;m->memdesc.gpuaddr=0;close_ok();cases++;
 setup();probe_ok();m=memory(PAGE_SIZE);assert(!kgsl_map_global_allocation(&gpu,m,0));unmap_error=-EIO;assert(kgsl_iommu_close(&gpu.mmu)==-EIO&&m->memdesc.priv&KGSL_MEMDESC_MAPPED&&enabled);unmap_error=0;close_ok();cases++;
 setup();probe_ok();assert(global_get_offset(&gpu,0,0)==(u64)-EINVAL&&global_get_offset(&gpu,1,0)==(u64)-EINVAL&&global_get_offset(&gpu,KGSL_IOMMU_GLOBAL_MEM_SIZE+PAGE_SIZE,0)==(u64)-EINVAL);close_ok();cases++;
 setup();probe_ok();assert(global_get_offset(&gpu,KGSL_IOMMU_GLOBAL_MEM_SIZE,KGSL_MEMDESC_RANDOM)==0);assert(global_get_offset(&gpu,PAGE_SIZE,0)==(u64)-ENOMEM);close_ok();cases++;
 setup();probe_ok();m=memory(PAGE_SIZE);m->memdesc.secure=true;assert(kgsl_map_global_allocation(&gpu,m,0)==-EOPNOTSUPP&&!gpu.global_map);close_ok();cases++;
 setup();probe_ok();m=memory(PAGE_SIZE);assert(kgsl_map_global_allocation(&gpu,m,1)==-EINVAL&&!gpu.global_map);close_ok();cases++;
 setup();struct kgsl_memdesc fixed={.sgt=calloc(1,sizeof(struct sg_table))};kgsl_fixed_global_free(&fixed);assert(!fixed.sgt&&sg_frees==1);balanced();cases++;
 setup();bank_error=-EIO;flush_error=-EIO;assert(kgsl_iommu_probe(&gpu)==-EIO&&domain_count==1);dispose_failed_fixture();cases++;
 setup();probe_ok();m=memory(PAGE_SIZE);assert(!kgsl_map_global_allocation(&gpu,m,0));gpu.mmu.iommu.started=true;assert(kgsl_iommu_close(&gpu.mmu)==-EBUSY&&(m->memdesc.priv&KGSL_MEMDESC_MAPPED)&&m->memdesc.gpuaddr&&!disable_calls);gpu.mmu.iommu.started=false;close_ok();cases++;
 setup();probe_ok();m=memory(PAGE_SIZE);assert(!kgsl_map_global_allocation(&gpu,m,0));m->memdesc.priv|=KGSL_MEMDESC_GMU_MAPPED;
 assert(kgsl_iommu_close(&gpu.mmu)==-EBUSY && enabled && m->memdesc.gpuaddr && (m->memdesc.priv&KGSL_MEMDESC_MAPPED));
 m->memdesc.priv&=~KGSL_MEMDESC_GMU_MAPPED;close_ok();cases++;
 setup();int before_sg_frees=sg_frees;struct kgsl_memdesc gmu_fixed={.sgt=calloc(1,sizeof(struct sg_table)),.priv=KGSL_MEMDESC_GMU_MAPPED};
 kgsl_fixed_global_free(&gmu_fixed);assert(gmu_fixed.sgt && sg_frees==before_sg_frees);
 gmu_fixed.priv=0;kgsl_fixed_global_free(&gmu_fixed);assert(!gmu_fixed.sgt && sg_frees==before_sg_frees+1);balanced();cases++;
 setup();checked_unmap_error=-EACCES;assert(kgsl_iommu_probe(&gpu)==-EACCES && checked_unmap_calls==1);balanced();cases++;
 setup();probe_ok();assert(checked_unmap_calls==1);close_ok();cases++;
 printf("%d parent/probe/global ownership scenarios passed; kernel/SMMU/io-pgtable hardware modeled\n",cases);return 0;
}
