/* Actual acquisition/probe functions; resources, locks and kernel containers mocked. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EPROBE_DEFER 517
#define ENOTSUPP 524
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define PTR_ERR_OR_ZERO(p) (IS_ERR(p) ? PTR_ERR(p) : 0)
#define GFP_KERNEL 0
#define KGSL_MMU_GLOBAL_PT 0
#define KGSL_MMU_SECURE_PT 1
#define KGSL_MMU_GLOBAL_LPAC_PT 2
#define KGSL_MMU_GLOBAL_PAGETABLE 0
#define KGSL_MMU_PAGED 2
#define KGSL_MMU_TYPE_IOMMU 0
#define KGSL_MMU_TYPE_NONE 1
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define IS_ENABLED(x) 0
#define MMU_OP_VALID(m, op) ((m) && (m)->mmu_ops && (m)->mmu_ops->op)
#define KGSL_MMU_DEVICE(m) ((struct kgsl_device *)((char *)(m) - offsetof(struct kgsl_device, mmu)))
#define KGSL_IOMMU_PRIV(d) (&(d)->mmu.priv.iommu)
#define dev_err(d, ...) ((void)(d))
#define dev_err_once(d, ...) ((void)(d))
typedef uint32_t u32;
struct list_head { struct list_head *next, *prev; };
#define container_of(p, type, member) ((type *)((char *)(p) - offsetof(type, member)))
#define list_for_each_entry(p, head, member) \
 for (struct list_head *n = (head)->next; n != (head) && ((p) = container_of(n, __typeof__(*(p)), member), 1); n = n->next)
static void list_init(struct list_head *h) { h->next = h->prev = h; }
#define INIT_LIST_HEAD(h) list_init(h)
static void list_add(struct list_head *n, struct list_head *h)
{ n->next = h->next; n->prev = h; h->next->prev = n; h->next = n; }
struct kref { int refs; };
static void kref_init(struct kref *k) { k->refs = 1; }
static bool kref_get_unless_zero(struct kref *k) { if (!k->refs) return false; ++k->refs; return true; }
#define spin_lock_init(p) (*(p) = 0)
#define spin_lock_irqsave(p, f) do { (void)(p); (f) = 0; } while (0)
#define spin_unlock_irqrestore(p, f) do { (void)(p); (void)(f); } while (0)
#define INIT_WORK(p, fn) (*(p) = 1)
#define atomic_set(p, v) (*(p) = (v))
#define atomic_long_set(p, v) (*(p) = (v))
static bool test_bit(int bit, const unsigned long *bits) { return (*bits >> bit) & 1; }
struct kgsl_mmu;
struct kgsl_pagetable {
 int lock, destroy_ws,destroy_error; void *priv; struct device *retained_dev; struct kref refcount; struct list_head list;
 struct { int entries; long mapped, max_mapped; } stats;
 struct kgsl_mmu *mmu; unsigned int name;
};
struct device {int refs;};
struct kgsl_mmu_ops {int (*mmu_init_pt)(struct kgsl_mmu *,struct kgsl_pagetable *);struct kgsl_pagetable *(*mmu_getpagetable)(struct kgsl_mmu *,unsigned long);};
struct kgsl_mmu {unsigned long features;const struct kgsl_mmu_ops *mmu_ops;struct kgsl_pagetable *defaultpagetable;};
struct kgsl_device {struct device *dev;struct kgsl_mmu mmu;};
static struct {int ptlock;struct list_head pagetable_list;}kgsl_driver;
static struct list_head retained_pagetables;
static int allocs,frees,sysfs_adds,init_calls,init_error,fail_alloc,module_refs;
static unsigned last_init_name;
static struct kgsl_device device;
static struct device device_stub;
static const struct kgsl_mmu_ops kgsl_iommu_ops;
#define THIS_MODULE (&module_refs)
static void __module_get(int *p){(*p)++;}
static struct device *get_device(struct device *d){d->refs++;return d;}
#define list_add_tail list_add
static void *kzalloc(size_t size,int flags){if(fail_alloc)return NULL;allocs++;return calloc(1,size);}
static void kfree(void *p){if(p){frees++;free(p);}}
static void pagetable_add_sysfs_objects(struct kgsl_pagetable *p){sysfs_adds++;}
static int init_pt(struct kgsl_mmu *m,struct kgsl_pagetable *p){init_calls++;last_init_name=p->name;return init_error;}
/* Native provider/probe lifecycle is covered by kgsl_parent_test.c. */
static int kgsl_iommu_probe(struct kgsl_device *d){return init_error;}
#include "kgsl-mmu-required-functions.h"
static const struct kgsl_mmu_ops kgsl_iommu_ops={init_pt,kgsl_iommu_getpagetable};
static void cleanup(void){struct list_head *head=&kgsl_driver.pagetable_list;if(head->next)while(head->next!=head){struct list_head *n=head->next;n->next->prev=head;head->next=n->next;kfree(container_of(n,struct kgsl_pagetable,list));}assert(allocs==frees&&!module_refs);}
static void reset(void){cleanup();memset(&device,0,sizeof(device));device.dev=&device_stub;device.mmu.mmu_ops=&kgsl_iommu_ops;list_init(&kgsl_driver.pagetable_list);list_init(&retained_pagetables);allocs=frees=sysfs_adds=init_calls=init_error=fail_alloc=0;last_init_name=0;}
static int cases;
static void probe_errors(void){const int errors[]={-EPROBE_DEFER,-ENOTSUPP,-EOPNOTSUPP,-ENOMEM,-EINVAL,-EIO,-ETIMEDOUT,0};for(size_t i=0;i<ARRAY_SIZE(errors);i++){reset();init_error=errors[i];assert(kgsl_mmu_probe(&device)==errors[i]&&!allocs&&kgsl_mmu_is_perprocess(&device.mmu));cases++;}}
static void acquire_errors(void)
{
 const int errors[] = {-ENOTSUPP, -EOPNOTSUPP, -ENOMEM, -EIO, -EPROBE_DEFER};
 const unsigned long names[] = {0, 1, 2, 1000, 1001};
 for (size_t i = 0; i < ARRAY_SIZE(errors); ++i) {
  reset();
  struct kgsl_pagetable *global = kgsl_mmu_getpagetable(&device.mmu, 0);
  device.mmu.defaultpagetable = global;
  init_error = errors[i];
  assert(PTR_ERR(kgsl_mmu_getpagetable(&device.mmu, 1000)) == errors[i]);
  assert(allocs == 2 && frees == 1 && sysfs_adds == 1 && global->refcount.refs == 1);
  assert(kgsl_mmu_is_perprocess(&device.mmu));
  assert(kgsl_get_pagetable(1000) == NULL);
  init_error = 0;
  struct kgsl_pagetable *pt = kgsl_mmu_getpagetable(&device.mmu, 1000);
  assert(pt != global && !IS_ERR(pt) && pt->name == 1000 && pt->refcount.refs == 1);
  assert(sysfs_adds == 2 && init_calls == 3); ++cases;
 }
 for (size_t i = 0; i < ARRAY_SIZE(names); ++i) {
  reset();
  struct kgsl_pagetable *pt = kgsl_mmu_getpagetable(&device.mmu, names[i]);
  assert(!IS_ERR(pt) && pt->name == names[i] && last_init_name == names[i]);
  assert(pt->mmu == &device.mmu && pt->refcount.refs == 1);
  assert(kgsl_mmu_getpagetable(&device.mmu, names[i]) == pt);
  assert(pt->refcount.refs == 2 && allocs == 1 && init_calls == 1 && sysfs_adds == 1); ++cases;
  device.mmu.features |= 1UL << KGSL_MMU_GLOBAL_PAGETABLE;
  assert(PTR_ERR(kgsl_mmu_getpagetable(&device.mmu, names[i])) == -EOPNOTSUPP);
  assert(pt->refcount.refs == 2 && allocs == 1 && init_calls == 1); ++cases;
 }
 reset(); fail_alloc = 1;
 assert(PTR_ERR(kgsl_mmu_getpagetable(&device.mmu, 1000)) == -ENOMEM);
 assert(!allocs && !init_calls && !sysfs_adds); ++cases;
 reset();
 struct kgsl_mmu_ops ops = kgsl_iommu_ops;
 device.mmu.mmu_ops = &ops; ops.mmu_init_pt = NULL;
 assert(PTR_ERR(kgsl_mmu_getpagetable(&device.mmu, 1000)) == -ENODEV);
 assert(!allocs && !init_calls && !sysfs_adds); ++cases;
 ops.mmu_getpagetable = NULL;
 assert(PTR_ERR(kgsl_mmu_getpagetable(&device.mmu, 1000)) == -ENODEV); ++cases;
 device.mmu.mmu_ops = NULL;
 assert(PTR_ERR(kgsl_mmu_getpagetable(&device.mmu, 1000)) == -ENODEV);
 assert(PTR_ERR(kgsl_mmu_createpagetableobject(&device.mmu, 1000)) == -ENODEV);
 assert(!allocs && !init_calls && !sysfs_adds); ++cases;
 reset();
 struct kgsl_pagetable *a = kgsl_mmu_getpagetable(&device.mmu, 1000);
 struct kgsl_pagetable *b = kgsl_mmu_getpagetable(&device.mmu, 1001);
 assert(!IS_ERR(a) && !IS_ERR(b) && a != b && a->name != b->name);
 assert(allocs == 2 && init_calls == 2 && sysfs_adds == 2); ++cases;
}
int main(void){probe_errors();acquire_errors();cleanup();printf("%d MMU acquisition/probe forwarding scenarios passed; native probe tested separately\n",cases);return 0;}
