#define KGSL_MEMDESC_GMU_MAPPED (1U << 11)
#define KGSL_MEMDESC_MAP_UNCERTAIN (1U << 10)
/* Real release functions, mocked backend and accounting. Not hardware proof. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define KGSL_MEMDESC_MAPPED 1
#define KGSL_MEMFLAGS_USERMEM_ION 2
#define GLOBAL 4
struct kgsl_memdesc;
struct kgsl_pagetable;
struct kgsl_device { int unused; };
struct kgsl_mmu { struct kgsl_device *device; };
#define KGSL_MMU_DEVICE(m) ((m)->device)
struct pt_ops {
 int (*mmu_unmap)(struct kgsl_pagetable *, struct kgsl_memdesc *);
 void (*put_gpuaddr)(struct kgsl_memdesc *);
 int (*mmu_map)(struct kgsl_pagetable *, struct kgsl_memdesc *);
};
struct kgsl_pagetable {
 struct kgsl_mmu *mmu; struct pt_ops *pt_ops;
 struct {int entries; long mapped, max_mapped;} stats;
};
struct mem_ops { void (*free)(struct kgsl_memdesc *); };
struct kgsl_memdesc {
 uint64_t size, gpuaddr; unsigned int priv, flags;
 struct kgsl_pagetable *pagetable; struct mem_ops *ops;
};
#define PT_OP_VALID(p, op) ((p) && (p)->pt_ops && (p)->pt_ops->op)
static bool kgsl_memdesc_is_global(struct kgsl_memdesc *m) { return m->priv & GLOBAL; }
static uint64_t kgsl_memdesc_footprint(struct kgsl_memdesc *m) { return m->size; }
#define atomic_inc(p) (++*(p))
#define KGSL_STATS_ADD(n,p,m) (*(p)+=(n),*(m)=*(p))
#define atomic_dec(p) (--*(p))
#define atomic_long_sub(v,p) (*(p) -= (v))
static int unmaps, address_releases, frees, traces, result, maps;
static bool uncertain;
static void kgsl_mmu_trace_gpu_mem_pagetable(struct kgsl_pagetable *p) { ++traces; }
static void kgsl_trace_gpu_mem_total(struct kgsl_device *d, long n) { ++traces; }
static int unmap(struct kgsl_pagetable *p, struct kgsl_memdesc *m) { ++unmaps; return result; }
static int map(struct kgsl_pagetable *p, struct kgsl_memdesc *m) { ++maps; if(uncertain) m->priv |= KGSL_MEMDESC_MAP_UNCERTAIN; return result; }
static void put(struct kgsl_memdesc *m) { ++address_releases; }
static void release(struct kgsl_memdesc *m) { ++frees; m->size = 0; }
#include "kgsl-release-functions.h"
int main(void)
{
 struct kgsl_device device = {0}; struct kgsl_mmu mmu = {&device};
 struct pt_ops ops = {unmap, put, map}; struct mem_ops memops = {release};
 int cases = 0;
 int errors[] = {-EIO, -ENOMEM, -ETIMEDOUT, -EUCLEAN};
 for (unsigned i=0; i<sizeof(errors)/sizeof(errors[0]); ++i) {
  struct kgsl_pagetable pt = {&mmu, &ops, {1,4096,4096}};
  struct kgsl_memdesc m = {4096,0x10000,KGSL_MEMDESC_MAPPED,0,&pt,&memops};
  struct kgsl_memdesc before = m;
  result=errors[i]; unmaps=address_releases=frees=traces=0;
  assert(kgsl_sharedmem_free(&m)==errors[i]);
  assert(!memcmp(&m,&before,sizeof(m)));
  assert(unmaps==1 && !address_releases && !frees && !traces);
  assert(pt.stats.entries==1 && pt.stats.mapped==4096); ++cases;
 }
 for (int missing=0; missing<3; ++missing) {
  struct pt_ops local = ops;
  struct kgsl_pagetable pt = {&mmu, &local, {1,4096,4096}};
  struct kgsl_memdesc m = {4096,0x10000,KGSL_MEMDESC_MAPPED,0,&pt,&memops};
  if (missing==0) local.mmu_unmap=NULL;
  if (missing==1) local.put_gpuaddr=NULL;
  if (missing==2) m.pagetable=NULL;
  struct kgsl_memdesc before=m;
  result=0; unmaps=address_releases=frees=traces=0;
  assert(kgsl_sharedmem_free(&m)==-ENODEV);
  assert(!memcmp(&m,&before,sizeof(m)) && !unmaps && !address_releases && !frees && !traces); ++cases;
 }
 for (int mapped=0; mapped<2; ++mapped) {
  struct kgsl_pagetable pt = {&mmu,&ops,{mapped,mapped*4096,mapped*4096}};
  struct kgsl_memdesc m = {4096,0x10000,mapped,0,&pt,&memops};
  result=0; unmaps=address_releases=frees=traces=0;
  assert(!kgsl_sharedmem_free(&m));
  assert(unmaps==mapped && address_releases==1 && frees==1);
  assert(!m.pagetable && !m.gpuaddr && !(m.priv & KGSL_MEMDESC_MAPPED));
  assert(!pt.stats.entries && !pt.stats.mapped); ++cases;
 }
 assert(!kgsl_sharedmem_free(NULL)); ++cases;
 struct kgsl_memdesc empty={0}; assert(!kgsl_sharedmem_free(&empty)); ++cases;

 for (int mapped=0; mapped<2; ++mapped) {
  struct kgsl_pagetable pt={&mmu,&ops,{mapped,mapped*4096,mapped*4096}};
  struct kgsl_memdesc m={4096,0x10000,KGSL_MEMDESC_MAP_UNCERTAIN | mapped,0,&pt,&memops};
  struct kgsl_memdesc before=m;unmaps=address_releases=frees=traces=maps=0;result=0;
  assert(kgsl_sharedmem_free(&m)==-EUCLEAN);
  assert(kgsl_mmu_unmap(&pt,&m)==-EUCLEAN && kgsl_mmu_map(&pt,&m)==-EUCLEAN);
  assert(!memcmp(&m,&before,sizeof(m)) && !unmaps && !maps && !address_releases && !frees && !traces);
  assert(pt.stats.entries==mapped && pt.stats.mapped==mapped*4096);++cases;
 }
 for (int scenario=0;scenario<4;++scenario) {
  struct pt_ops local=ops;struct kgsl_pagetable pt={&mmu,&local,{0,0,0}};
  struct kgsl_memdesc m={4096,0x10000,0,0,&pt,&memops};
  maps=unmaps=address_releases=frees=traces=0;uncertain=scenario==1;result=scenario<2?-ENOMEM:0;
  if(scenario==2)local.mmu_map=NULL;
  assert(kgsl_mmu_map(&pt,&m)==(scenario==2?-ENODEV:result));
  assert(maps==(scenario!=2));
  assert(pt.stats.entries==(scenario==3) && pt.stats.mapped==(scenario==3?4096:0));
  result=0;
  assert(kgsl_sharedmem_free(&m)==(scenario==1?-EUCLEAN:0));
  assert(frees==(scenario!=1) && address_releases==(scenario!=1));
  if(scenario==1)assert(m.gpuaddr && m.pagetable==&pt && !unmaps && !traces);
  ++cases;
 }
 printf("%d release primitive scenarios passed; outer owner lifecycle not covered\n",cases);
 return 0;
}
