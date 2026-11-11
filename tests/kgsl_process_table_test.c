/* Caller/backend integration. Kernel page-table allocation and hardware are modeled. */
#include <stddef.h>
struct work_struct { int unused; };
struct list_head { struct list_head *next, *prev; };
struct kgsl_mmu;
struct kgsl_mmu_pt_ops;
#define KGSL_PAGETABLE_TEST_FIELDS struct kgsl_mmu *mmu; void *priv; unsigned name; const struct kgsl_mmu_pt_ops *pt_ops; unsigned long long fault_addr; struct list_head list; struct work_struct destroy_ws; int destroy_error; struct device *retained_dev;
#define KGSL_SMMU_TEST_FIELDS int (*terminate_fault)(const void *); int (*rearm_fault)(const void *);
#define main backend_scenarios
#include "kgsl_pagetable_test.c"
#undef main
#define BIT(n) (1UL << (n))
#define READ_ONCE(v) (v)
#define WRITE_ONCE(v,x) ((v)=(x))
#define IS_ERR_OR_NULL(p) (!(p)||IS_ERR(p))
#define SSIZE_MAX INT64_MAX
#define KGSL_MMU_64BIT 1
#define KGSL_MMU_SPLIT_TABLES_GC 2
#define KGSL_MMU_SPLIT_TABLES_LPAC 3
#define KGSL_MMU_SECURE_PT 1
#define KGSL_MEMDESC_MAP_UNCERTAIN BIT(0)
#define KGSL_MEMDESC_PRIVILEGED BIT(1)
#define KGSL_MEMDESC_UCODE BIT(2)
#define KGSL_MEMFLAGS_GPUREADONLY BIT(0)
#define KGSL_MEMFLAGS_IOCOHERENT BIT(1)
#define KGSL_MEMFLAGS_SECURE BIT(2)
#define IOMMU_NOEXEC BIT(2)
#define IOMMU_PRIV BIT(3)
#define IOMMU_CACHE BIT(4)
#define IOMMU_USE_UPSTREAM_HINT BIT(5)
#define IOMMU_USE_LLC_NWA BIT(6)
#define DOMAIN_ATTR_USE_UPSTREAM_HINT 1
#define DOMAIN_ATTR_USE_LLC_NWA 2
#define DOMAIN_ATTR_CONTEXT_BANK 3
#define __GFP_ZERO 0
#define __GFP_NORETRY 0
#define __GFP_HIGHMEM 0
static bool test_warn(bool value){assert(!value);return value;}
#define WARN_ON(v) test_warn(v)
static void test_log(const char *fmt,...) {}
#define dev_err(d,...) ((void)(d),test_log(__VA_ARGS__))
#define trace_kgsl_pagetable_destroy(...) ((void)0)
struct rb_root { void *rb_node; };
#define RB_ROOT ((struct rb_root){NULL})
#define RB_EMPTY_ROOT(r) (!(r)->rb_node)
struct kgsl_iommu_pt { struct kgsl_iommu_table *table,*bootstrap; bool submitted,bootstrap_enabled,poisoned; void *domain; bool attached; u64 ttbr0; u32 contextidr;struct rb_root rbtree; };
struct kgsl_iommu_context { int cb_num; struct kgsl_pagetable *default_pt; };
struct platform_device { struct device dev; };
struct kgsl_iommu { bool ppt_active,started,pm_held; struct device *smmu_dev; struct kgsl_pagetable *failed_parent; struct kgsl_iommu_context user_context,lpac_context,secure_context; struct platform_device *pdev; };
struct kgsl_mmu_ops { int (*mmu_close)(struct kgsl_mmu *); };
struct kgsl_mmu { const struct kgsl_mmu_ops *mmu_ops; unsigned long features;struct kgsl_iommu iommu;struct kgsl_pagetable *defaultpagetable,*lpac_pagetable,*securepagetable;struct kgsl_device *device; };
struct kgsl_device { void *global_map; void *events_wq;int context_idr,timelines;struct device *dev;struct kgsl_mmu mmu;struct adreno_smmu_priv smmu;struct list_head globals; };
struct kgsl_memdesc { u64 gpuaddr,size,flags,priv,padding;void *pages;struct sg_table *sgt; };
struct kgsl_global_memdesc { struct list_head node;struct kgsl_memdesc memdesc; };
struct kgsl_mmu_pt_ops { int (*mmu_destroy_pagetable)(struct kgsl_pagetable *); };
static const struct kgsl_mmu_pt_ops iommu_pt_ops;
#define KGSL_MMU_DEVICE(m) ((m)->device)
#define _IOMMU_PRIV(m) (&(m)->iommu)
static bool test_bit(int bit,const unsigned long *p){return !!(*p&BIT(bit));}
static void setup_64bit_pagetable(struct kgsl_mmu *m,struct kgsl_pagetable *p,struct kgsl_iommu_pt *i){}
static void setup_32bit_pagetable(struct kgsl_mmu *m,struct kgsl_pagetable *p,struct kgsl_iommu_pt *i){assert(!"32-bit native table");}
static bool parent_mode;
static int parent_cookie, parent_map_error, parent_guard_error, parent_drain_error, parent_map_calls, parent_guard_calls, parent_drain_calls;
static bool parent_short_unmap;
static size_t parent_bytes;
static int parent_drain(const void *cookie){assert(parent_mode&&dev.pm_refs>0&&cookie==&parent_cookie);parent_drain_calls++;return parent_drain_error;}
static int iommu_map(void *d,u64 va,phys_addr_t pa,size_t n,int f,int g){assert(parent_mode&&dev.pm_refs>0&&d==&parent_cookie&&va>=0xffffff8000000000ULL&&n==PAGE_SIZE&&!(f&IOMMU_WRITE));parent_guard_calls++;if(parent_guard_error)return parent_guard_error;parent_bytes+=n;return 0;}
static size_t iommu_unmap(void *d,u64 va,size_t n){assert(parent_mode&&dev.pm_refs>0&&d==&parent_cookie&&va>=0xffffff8000000000ULL&&n<=parent_bytes);size_t done=parent_short_unmap?n-PAGE_SIZE:n;parent_bytes-=done;return done;}
static long iommu_map_sg(void *d,u64 va,struct scatterlist *s,int n,int f,int g){assert(parent_mode&&dev.pm_refs>0&&d==&parent_cookie&&va==0xffffff8000000000ULL&&n==2);parent_map_calls++;if(parent_map_error)return parent_map_error;size_t bytes=0;for(int i=0;i<n;i++,s=s->next)bytes+=s->length;parent_bytes+=bytes;return bytes;}
struct page { phys_addr_t phys; };
static struct page guard_page={0xc00000},*kgsl_guard_page,*kgsl_secure_guard_page;
static struct page *alloc_page(int flags){return &guard_page;}
static struct page *kgsl_alloc_secure_page(void){assert(!"secure native map");return NULL;}
#define page_to_phys(p) ((p)->phys)
static bool kgsl_memdesc_is_secured(struct kgsl_memdesc *m){return !!(m->flags&KGSL_MEMFLAGS_SECURE);}
static u64 kgsl_memdesc_footprint(struct kgsl_memdesc *m){return m->size+m->padding;}
static int sgt_frees;
static struct sg_table *kgsl_alloc_sgt_from_pages(struct kgsl_memdesc *m){return m->sgt;}
static void kgsl_free_sgt(struct sg_table *s){sgt_frees++;}
static bool list_empty(struct list_head *l){return !l->next||l->next==l;}
#define list_for_each_entry(p,h,m) for((void)(h),(p)=NULL;(p)!=NULL;)
static int kgsl_iommu_unmap_globals(struct kgsl_mmu *m,struct kgsl_pagetable *p){assert(!"legacy globals");return -EIO;}
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define PT_OP_VALID(p,op) ((p)->pt_ops && (p)->pt_ops->op)
static struct { int ptlock; } kgsl_driver;
static struct list_head retained_pagetables;
static int retained_count,module_refs,pt_locked;
#define THIS_MODULE (&module_refs)
static void __module_get(int *m){(*m)++;}
#define spin_lock_irqsave(p,f) do {assert((p)==&kgsl_driver.ptlock&&!pt_locked);pt_locked=1;(f)=0;}while(0)
#define spin_unlock_irqrestore(p,f) do {assert(pt_locked);pt_locked=0;(void)(f);}while(0)
static void list_add_tail(struct list_head *l,struct list_head *head){assert(head==&retained_pagetables&&pt_locked);retained_count++;l->next=head;}
static int refcount_read(int *ref){return *ref;}
static void __free_page(struct page *p){assert(!"close with live children");}
static void *addr_entry_cache;
static void kmem_cache_destroy(void *c){assert(!"close with live children");}
#define MMU_OP_VALID(m,op) ((m)->mmu_ops&&(m)->mmu_ops->op)
static int global_frees,power_closes;
static void destroy_workqueue(void *w){}
static void kgsl_device_snapshot_close(struct kgsl_device *d){}
static void idr_destroy(int *i){}
static void kgsl_device_events_remove(struct kgsl_device *d){}
static void kgsl_free_globals(struct kgsl_device *d){global_frees++;}
static void kgsl_pwrctrl_close(struct kgsl_device *d){power_closes++;}
static void kgsl_device_debugfs_close(struct kgsl_device *d){}
static void _unregister_device(struct kgsl_device *d){}
struct adreno_device { struct kgsl_device *device; };
struct adreno_ringbuffer { struct adreno_device *device; };
#define ADRENO_RB_DEVICE(rb) ((rb)->device)
#define KGSL_DEVICE(a) ((a)->device)
#define KGSL_IOMMU_PRIV(d) (&(d)->mmu.iommu)
static bool adreno_is_a6xx(struct adreno_device *a){return true;}
static bool adreno_is_a5xx(struct adreno_device *a){return false;}
static bool adreno_is_a3xx(struct adreno_device *a){return false;}
static struct kgsl_pagetable *command_pt;
static u64 kgsl_mmu_pagetable_get_ttbr0(struct kgsl_pagetable *p){return ((struct kgsl_iommu_pt *)p->priv)->ttbr0;}
static u32 kgsl_mmu_pagetable_get_contextidr(struct kgsl_pagetable *p){return ((struct kgsl_iommu_pt *)p->priv)->contextidr;}
static unsigned _adreno_iommu_set_pt_v2_a6xx(struct kgsl_device *d,unsigned *cmds,u64 root,u32 id,struct adreno_ringbuffer *rb,int cb){
 struct kgsl_iommu_pt *i=command_pt->priv;
 assert(i->submitted&&root==i->table->ttbr0&&id==command_pt->name&&cb==3);
 *cmds=0xabc;return 1;
}
static unsigned _adreno_iommu_set_pt_v2_a5xx(struct kgsl_device *d,unsigned *cmds,u64 root,u32 id,struct adreno_ringbuffer *rb){assert(!"unexpected a5xx");return 0;}
static unsigned _adreno_iommu_set_pt_v2_a3xx(struct kgsl_device *d,unsigned *cmds,u64 root,u32 id){assert(!"unexpected a3xx");return 0;}
static int kgsl_iommu_release_parent(struct kgsl_pagetable *p){assert(!"parent release with live children");return -EIO;}
#include "process-functions.h"
static const struct kgsl_mmu_ops mmu_ops={kgsl_iommu_close};
static const struct kgsl_mmu_pt_ops iommu_pt_ops={kgsl_iommu_destroy_pagetable};
static struct kgsl_device gpu;
static struct kgsl_iommu_pt parent_pt;
static struct scatterlist sg[2];
static struct sg_table sgt;
static void init_fixture(void){
 setup();memset(&gpu,0,sizeof(gpu));memset(&parent_pt,0,sizeof(parent_pt));
 gpu.dev=&dev;gpu.mmu.device=&gpu;gpu.mmu.mmu_ops=&mmu_ops;global_frees=power_closes=0;gpu.mmu.features=BIT(KGSL_MMU_64BIT)|BIT(KGSL_MMU_SPLIT_TABLES_GC);gpu.smmu=priv;gpu.mmu.defaultpagetable=&parent;parent.priv=&parent_pt;parent.mmu=&gpu.mmu;parent_pt.attached=true;parent_pt.bootstrap_enabled=true;
 sg[0]=(struct scatterlist){.phys=0x400000,.dma=0xf0000000,.length=2*PAGE_SIZE,.next=&sg[1]};sg[1]=(struct scatterlist){.phys=0x800000,.dma=0xf4000000,.length=3*PAGE_SIZE};sgt=(struct sg_table){sg,1,2};
 kgsl_guard_page=NULL;sgt_frees=0;retained_count=module_refs=pt_locked=0;parent_mode=false;parent_map_error=parent_guard_error=parent_drain_error=parent_map_calls=parent_guard_calls=parent_drain_calls=0;parent_short_unmap=false;parent_bytes=0;
}
static struct kgsl_pagetable *new_process(unsigned name){
 struct kgsl_pagetable *p=calloc(1,sizeof(*p));p->name=name;p->mmu=&gpu.mmu;p->refcount.count=1;assert(!_init_per_process_pt(&gpu.mmu,p));return p;
}
static struct kgsl_memdesc memory(void){return (struct kgsl_memdesc){.gpuaddr=0x4000000000,.size=5*PAGE_SIZE,.padding=PAGE_SIZE,.sgt=&sgt};}
static void destroy_process(struct kgsl_pagetable *p){assert(!kgsl_iommu_destroy_pagetable(p));free(p);}
/* Tear down simulated retained state; production retirement is deliberately not implied. */
static void dispose_retained_fixture(struct kgsl_pagetable *p){
 struct kgsl_iommu_pt *i=p->priv;
 if(i->table->poisoned)dispose_poisoned_fixture(i->table);
 else {i->submitted=false;i->rbtree=RB_ROOT;assert(!kgsl_iommu_table_destroy(i->table));}
 free(i);free(p);if(retained_count){assert(module_refs==1);module_refs--;retained_count--;dev.refs--;}
}
static void parent_mapping_fixture(void){init_fixture();parent_mode=true;parent_pt.domain=&parent_cookie;gpu.smmu.cookie=&parent_cookie;gpu.smmu.flush_tlb=parent_drain;assert(!pm_runtime_resume_and_get(&dev));}
static void dispose_parent_mapping_fixture(void){parent_bytes=0;pm_runtime_put_autosuspend(&dev);balanced();}
int main(void){
 backend_scenarios();int cases=0;
 init_fixture();struct kgsl_pagetable *a=new_process(77),*b=new_process(88);struct kgsl_iommu_pt *ia=a->priv,*ib=b->priv;
 assert(ia->table!=ib->table&&ia->ttbr0!=ib->ttbr0&&ia->contextidr==77&&ib->contextidr==88&&parent.refcount.count==3&&kgsl_iommu_get_context_bank(a)==3&&!ia->domain);cases++;
 assert(kgsl_mmu_close(&gpu)==-EBUSY);kgsl_device_platform_remove(&gpu);assert(!global_frees&&!power_closes&&parent.refcount.count==3);cases++;
 struct kgsl_memdesc ma=memory(),mb=memory();assert(!kgsl_iommu_map(a,&ma));sg[0].phys=0x900000;assert(!kgsl_iommu_map(b,&mb));struct fake_ops *fa=(void *)ia->table->ops,*fb=(void *)ib->table->ops;assert(fa->pa[lookup(fa,ma.gpuaddr)]==0x400000&&fb->pa[lookup(fb,mb.gpuaddr)]==0x900000);assert(!(fa->prot[lookup(fa,ma.gpuaddr+ma.size)]&IOMMU_WRITE));cases++;
 assert(!kgsl_iommu_unmap(a,&ma)&&!kgsl_iommu_unmap(b,&mb));destroy_process(a);destroy_process(b);balanced();cases++;
 init_fixture();a=new_process(77);ma=memory();ma.flags=KGSL_MEMFLAGS_GPUREADONLY|KGSL_MEMFLAGS_IOCOHERENT;ma.priv=KGSL_MEMDESC_PRIVILEGED|KGSL_MEMDESC_UCODE;ma.pages=&ma;assert(!kgsl_iommu_map(a,&ma)&&sgt_frees==1);ia=a->priv;fa=(void *)ia->table->ops;assert(fa->prot[lookup(fa,ma.gpuaddr)]==(IOMMU_READ|IOMMU_CACHE|IOMMU_PRIV));assert(!kgsl_iommu_unmap(a,&ma));destroy_process(a);balanced();cases++;
 init_fixture();a=new_process(77);ma=memory();map_fail_call=2;map_fail_prefix=1;assert(kgsl_iommu_map(a,&ma)==-ENOSPC&&!(ma.priv&KGSL_MEMDESC_MAP_UNCERTAIN));destroy_process(a);balanced();cases++;
 init_fixture();a=new_process(77);ma=memory();map_fail_call=3;assert(kgsl_iommu_map(a,&ma)==-ENOSPC&&!(ma.priv&KGSL_MEMDESC_MAP_UNCERTAIN));destroy_process(a);balanced();cases++;
 init_fixture();a=new_process(77);ma=memory();map_fail_call=2;map_fail_prefix=1;unmap_zero_call=1;assert(kgsl_iommu_map(a,&ma)==-EUCLEAN&&(ma.priv&KGSL_MEMDESC_MAP_UNCERTAIN));assert(kgsl_iommu_destroy_pagetable(a)==-EBUSY&&parent.refcount.count==2);dispose_retained_fixture(a);balanced();cases++;
 init_fixture();a=new_process(77);ma=memory();assert(!kgsl_iommu_map(a,&ma));flush_error=-ETIMEDOUT;assert(kgsl_iommu_unmap(a,&ma)==-EUCLEAN&&(ma.priv&KGSL_MEMDESC_MAP_UNCERTAIN));dispose_retained_fixture(a);balanced();cases++;
 init_fixture();a=new_process(77);ma=memory();pm_error=-EIO;assert(kgsl_iommu_map(a,&ma)==-EIO&&!(ma.priv&KGSL_MEMDESC_MAP_UNCERTAIN));_deferred_destroy(&a->destroy_ws);assert(retained_count==1&&module_refs==1&&a->destroy_error==-EIO&&a->retained_dev==&dev&&parent.refcount.count==2&&dev.refs==3);pm_error=0;dispose_retained_fixture(a);balanced();cases++;
 init_fixture();a=new_process(77);ia=a->priv;ia->submitted=true;_deferred_destroy(&a->destroy_ws);assert(retained_count==1&&a->destroy_error==-EBUSY&&parent.refcount.count==2&&live_tables==1);assert(kgsl_mmu_close(&gpu)==-EBUSY);dispose_retained_fixture(a);balanced();cases++;
 init_fixture();a=new_process(77);ia=a->priv;ia->rbtree.rb_node=ia;assert(kgsl_iommu_destroy_pagetable(a)==-EBUSY);ia->rbtree=RB_ROOT;destroy_process(a);balanced();cases++;
 init_fixture();a=new_process(77);_deferred_destroy(&a->destroy_ws);assert(!retained_count&&!module_refs);balanced();cases++;
 init_fixture();struct kgsl_pagetable child={.mmu=&gpu.mmu,.name=99};parent_pt.attached=false;assert(_init_per_process_pt(&gpu.mmu,&child)==-ENODEV&&!child.priv);balanced();cases++;
 init_fixture();gpu.mmu.features=BIT(KGSL_MMU_64BIT);assert(_init_per_process_pt(&gpu.mmu,&child)==-ENODEV&&!child.priv);balanced();cases++;
 init_fixture();metadata_error=-EIO;assert(_init_per_process_pt(&gpu.mmu,&child)==-EIO&&!child.priv&&!child.pt_ops);balanced();cases++;
 init_fixture();fail_alloc=1;assert(_init_per_process_pt(&gpu.mmu,&child)==-ENOMEM&&!child.priv&&!child.pt_ops);balanced();cases++;
 init_fixture();cfg.quirks=0;assert(_init_per_process_pt(&gpu.mmu,&child)==-EOPNOTSUPP&&!child.priv);balanced();cases++;
 init_fixture();a=new_process(77);ma=memory();ma.size=4*PAGE_SIZE;assert(kgsl_iommu_map(a,&ma)==-EINVAL&&!maps);destroy_process(a);balanced();cases++;
 init_fixture();a=new_process(77);ma=memory();ma.flags=KGSL_MEMFLAGS_SECURE;assert(kgsl_iommu_map(a,&ma)==-EOPNOTSUPP&&!maps);destroy_process(a);balanced();cases++;
 init_fixture();gpu.mmu.defaultpagetable=NULL;assert(_init_per_process_pt(&gpu.mmu,&child)==-ENODEV&&!child.priv);balanced();cases++;
 init_fixture();gpu.mmu.features=BIT(KGSL_MMU_SPLIT_TABLES_GC);assert(_init_per_process_pt(&gpu.mmu,&child)==-ENODEV&&!child.priv);balanced();cases++;
 init_fixture();a=new_process(77);command_pt=a;gpu.mmu.iommu.user_context.cb_num=3;
 struct adreno_device adreno={&gpu};struct adreno_ringbuffer rb={&adreno};unsigned cmd=0;
 assert(adreno_iommu_set_pt_generate_cmds(&rb,&cmd,a)==1&&cmd==0xabc);
 _deferred_destroy(&a->destroy_ws);assert(retained_count==1&&a->destroy_error==-EBUSY&&parent.refcount.count==2);
 dispose_retained_fixture(a);balanced();cases++;
 init_fixture();parent_pt.poisoned=true;assert(_init_per_process_pt(&gpu.mmu,&child)==-ENODEV&&!child.priv);balanced();cases++;
 init_fixture();parent_pt.bootstrap_enabled=false;assert(_init_per_process_pt(&gpu.mmu,&child)==-ENODEV&&!child.priv);balanced();cases++;
 parent_mapping_fixture();ma=memory();ma.gpuaddr=0x0001ff8000000000ULL;assert(!kgsl_iommu_map(&parent,&ma)&&parent_map_calls==1&&parent_guard_calls==1&&parent_bytes==ma.size+ma.padding);assert(!kgsl_iommu_unmap(&parent,&ma)&&parent_drain_calls==1&&!parent_bytes);dispose_parent_mapping_fixture();cases++;
 parent_mapping_fixture();ma=memory();ma.gpuaddr=0x0001ff8000000000ULL;parent_map_error=-ENOMEM;assert(kgsl_iommu_map(&parent,&ma)==-ENOMEM&&parent_pt.poisoned&&(ma.priv&KGSL_MEMDESC_MAP_UNCERTAIN));dispose_parent_mapping_fixture();cases++;
 parent_mapping_fixture();ma=memory();ma.gpuaddr=0x0001ff8000000000ULL;parent_guard_error=-EIO;assert(kgsl_iommu_map(&parent,&ma)==-EIO&&parent_pt.poisoned&&(ma.priv&KGSL_MEMDESC_MAP_UNCERTAIN)&&parent_drain_calls);dispose_parent_mapping_fixture();cases++;
 parent_mapping_fixture();ma=memory();ma.gpuaddr=0x0001ff8000000000ULL;assert(!kgsl_iommu_map(&parent,&ma));parent_short_unmap=true;assert(kgsl_iommu_unmap(&parent,&ma)==-EUCLEAN&&parent_pt.poisoned&&(ma.priv&KGSL_MEMDESC_MAP_UNCERTAIN));dispose_parent_mapping_fixture();cases++;
 parent_mapping_fixture();ma=memory();ma.gpuaddr=0x0001ff8000000000ULL;assert(!kgsl_iommu_map(&parent,&ma));parent_drain_error=-ETIMEDOUT;assert(kgsl_iommu_unmap(&parent,&ma)==-EUCLEAN&&parent_pt.poisoned&&(ma.priv&KGSL_MEMDESC_MAP_UNCERTAIN));dispose_parent_mapping_fixture();cases++;
 parent_mapping_fixture();ma=memory();ma.gpuaddr=0x0001ff8000000000ULL;ma.size=PAGE_SIZE;assert(kgsl_iommu_map(&parent,&ma)==-EINVAL&&!parent_map_calls);dispose_parent_mapping_fixture();cases++;
 printf("%d process caller/backend ownership scenarios passed; parent attach/retirement not exercised here\n",cases);return 0;
}
