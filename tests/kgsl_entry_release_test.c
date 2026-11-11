#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint64_t u64;
#define smp_store_release(p,v) __atomic_store_n((p),(v),__ATOMIC_RELEASE)

#define KGSL_MEM_ENTRY_KERNEL 0
#define THIS_MODULE NULL
#define pr_err(...) ((void)0)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
struct list_head { int linked; };
struct kref { int refs; };
struct kgsl_pagetable {struct kref refcount;};
struct kgsl_process_private {int mem_lock,mem_idr,refs;struct kgsl_pagetable *pagetable;};
struct kgsl_memdesc {u64 size,gpuaddr;struct kgsl_pagetable *pagetable;int lock;};
struct kgsl_mem_entry {u64 published_gpuaddr;struct kref refcount;struct kgsl_memdesc memdesc;struct kgsl_process_private *priv;unsigned int id;struct list_head retained_node;int release_error;struct kgsl_pagetable *retained_pt;};
static struct list_head kgsl_retained_entries;
static int kgsl_retained_lock;
static struct {struct {long mapped;} stats;} kgsl_driver;
static int unmap_error,free_error,unmaps,backing_calls,entries_freed,process_puts,stats_sub,removed,retained,module_refs,type;
static void mutex_lock(int *l) {assert(!*l);++*l;}
static void mutex_unlock(int *l) {assert(*l);--*l;}
static void spin_lock(int *l) {mutex_lock(l);}
static void spin_unlock(int *l) {mutex_unlock(l);}
static void kref_get(struct kref *k) {assert(k->refs>0);++k->refs;}
static void __module_get(void *m) {++module_refs;}
static void list_add_tail(struct list_head *n,struct list_head *h) {assert(!n->linked);n->linked=1;++retained;}
static void idr_remove(int *idr,unsigned int id) {assert(id);++removed;}
static int kgsl_mmu_put_gpuaddr(struct kgsl_memdesc *m) {++unmaps;if(unmap_error)return unmap_error;m->gpuaddr=0;m->pagetable=NULL;return 0;}
static int kgsl_sharedmem_free(struct kgsl_memdesc *m) {++backing_calls;if(free_error)return free_error;m->size=0;return 0;}
static void kgsl_process_private_put(struct kgsl_process_private *p) {assert(p && p->refs>0);--p->refs;++process_puts;}
static void kgsl_process_sub_stats(struct kgsl_process_private *p,unsigned int t,u64 size) {assert(p && p->refs>0 && size==4096);++stats_sub;}
static void atomic_long_sub(u64 n,long *v) {*v-=n;}
static unsigned int kgsl_memdesc_usermem_type(struct kgsl_memdesc *m) {return type;}
static void kfree(void *p) {++entries_freed;}
struct kgsl_device {int unused;};
#define GFP_KERNEL 0
#define GFP_NOWAIT 0
static int id_error, map_error, region_error, regions;
static int kgsl_mmu_set_svm_region(struct kgsl_pagetable *p,u64 addr,u64 size) {++regions;return region_error;}
static struct kgsl_pagetable *attach_pt;
static int kgsl_process_private_get(struct kgsl_process_private *p) {++p->refs;return 1;}
static int kgsl_mem_entry_track_gpuaddr(struct kgsl_device *d,struct kgsl_process_private *p,struct kgsl_mem_entry *e) {e->memdesc.gpuaddr=0x10000;e->memdesc.pagetable=attach_pt;return 0;}
static void idr_preload(int flags) {}
static void idr_preload_end(void) {}
static int idr_alloc(int *idr,void *ptr,int start,int end,int flags) {return id_error?id_error:7;}
static int kgsl_mmu_map(struct kgsl_pagetable *p,struct kgsl_memdesc *m) {assert(!container_of(m,struct kgsl_mem_entry,memdesc)->published_gpuaddr);return map_error;}
static void kgsl_memfree_purge(struct kgsl_pagetable *p,u64 addr,u64 size) {}
#include "kgsl-entry-release-functions.h"
static void reset(void) {unmap_error=free_error=unmaps=backing_calls=entries_freed=process_puts=stats_sub=removed=retained=module_refs=0;kgsl_driver.stats.mapped=4096;type=1;}
int main(void) {
 int cases=0;
 for(int stage=0;stage<2;++stage) {
  reset();struct kgsl_pagetable pt={{1}};struct kgsl_process_private proc={0,0,1,NULL};
  struct kgsl_mem_entry e={.memdesc={4096,0x10000,&pt,0},.priv=&proc,.id=7};
  if(stage==0)unmap_error=-EIO;else free_error=-ENOMEM;
  kgsl_mem_entry_destroy(&e.refcount);
  assert(e.release_error==(stage==0?-EIO:-ENOMEM));assert(e.priv==&proc && proc.refs==1);
  assert(!e.id && removed==1 && retained==1 && module_refs==1);
  assert(!entries_freed && !process_puts && !stats_sub && kgsl_driver.stats.mapped==4096);
  assert(pt.refcount.refs==(stage==0?2:1));assert(backing_calls==stage);
  int saved_unmaps=unmaps,saved_backing=backing_calls;
  kgsl_mem_entry_destroy(&e.refcount);kgsl_mem_entry_free_unattached(&e);
  assert(kgsl_mem_entry_detach_process(&e)==e.release_error);
  assert(unmaps==saved_unmaps && backing_calls==saved_backing && retained==1 && module_refs==1);cases+=2;
 }
 for(type=0;type<2;) {
  int saved=type;reset();type=saved;struct kgsl_pagetable pt={{1}};struct kgsl_process_private proc={0,0,1,NULL};
  struct kgsl_mem_entry e={.memdesc={4096,0x10000,&pt,0},.priv=&proc,.id=7};
  kgsl_mem_entry_destroy(&e.refcount);
  assert(entries_freed==1 && process_puts==1 && stats_sub==1 && proc.refs==0 && !e.priv);
  assert(!retained && !module_refs && kgsl_driver.stats.mapped==(type?0:4096));++cases;type=saved+1;
 }
 for(int fail=0;fail<2;++fail) {
  reset();struct kgsl_pagetable pt={{1}};struct kgsl_process_private proc={0,0,1,NULL};
  struct kgsl_mem_entry e={.memdesc={4096,0x10000,&pt,0},.priv=&proc,.id=7};
  unmap_error=fail?-EUCLEAN:0;assert(kgsl_mem_entry_detach_process(&e)==unmap_error);
  assert(removed==1 && !e.id && proc.refs==fail && retained==fail && process_puts==!fail);
  if(fail) {kgsl_mem_entry_free_unattached(&e);assert(!backing_calls && !entries_freed);}
  else {kgsl_mem_entry_free_unattached(&e);assert(backing_calls==1 && entries_freed==1);}
  assert(!stats_sub);++cases;
 }
 for(int fail=0;fail<2;++fail) {
  reset();struct kgsl_pagetable pt={{1}};struct kgsl_mem_entry e={.memdesc={4096,0x10000,&pt,0}};
  free_error=fail?-ETIMEDOUT:0;kgsl_mem_entry_free_unattached(&e);
  assert(entries_freed==!fail && retained==fail && pt.refcount.refs==1+fail);
  assert(!process_puts && !stats_sub);++cases;
 }
 for(int stage=0;stage<2;++stage) for(int fail=0;fail<2;++fail) {
  reset();struct kgsl_pagetable pt={{1}};struct kgsl_process_private proc={0,0,1,NULL};struct kgsl_device dev={0};
  struct kgsl_mem_entry e={.memdesc={4096,0,NULL,0}};
  attach_pt=&pt;id_error=stage==0?-ENOMEM:0;map_error=stage==1?-EINVAL:0;unmap_error=fail?-EUCLEAN:0;
  assert(kgsl_mem_entry_attach_process(&dev,&proc,&e)==(stage==0?-ENOMEM:-EINVAL));
  assert(!e.id && proc.refs==(fail?2:1) && retained==fail && !e.published_gpuaddr);
  assert((e.priv==&proc)==(bool)fail);
  kgsl_mem_entry_free_unattached(&e);
  assert(entries_freed==!fail && backing_calls==!fail && !stats_sub);++cases;
 }

 for(int scenario=0;scenario<5;++scenario) {
  reset();struct kgsl_pagetable pt={{1}};struct kgsl_process_private proc={0,0,1,&pt};
  struct kgsl_mem_entry e={.memdesc={4096,0,&pt,0},.priv=&proc,.id=7};
  regions=0;region_error=scenario==1?-ENOSPC:0;map_error=scenario>=3?-ENOMEM:0;
  unmap_error=scenario==4?-EUCLEAN:0;
  if(scenario==0)e.memdesc.gpuaddr=0x20000;
  unsigned long want=scenario==0?(unsigned long)-EBUSY:scenario==1?(unsigned long)-ENOSPC:scenario>=3?(unsigned long)-ENOMEM:0x10000;
  assert(_gpu_set_svm_region(&proc,&e,0x10000,4096)==want);
  assert(!e.memdesc.lock && regions==(scenario!=0));
  assert(e.published_gpuaddr==(scenario==2?0x10000:0));
  if(scenario==4) {
   assert(!e.id && e.release_error==-EUCLEAN && e.memdesc.gpuaddr==0x10000);
   assert(e.memdesc.pagetable==&pt && e.priv==&proc && proc.refs==1 && pt.refcount.refs==2);
   assert(retained==1 && module_refs==1 && !entries_freed && !process_puts && !backing_calls);
  } else {
   assert(e.id==7 && !retained && !module_refs && !process_puts);
   if(scenario==3)assert(!e.memdesc.gpuaddr && !e.memdesc.pagetable);
  }
  ++cases;
 }
 reset();struct kgsl_pagetable pt={{1}};struct kgsl_process_private proc={0,0,1,&pt};struct kgsl_device dev={0};
 struct kgsl_mem_entry e={.memdesc={4096,0,NULL,0}};attach_pt=&pt;id_error=map_error=unmap_error=0;
 assert(!kgsl_mem_entry_attach_process(&dev,&proc,&e) && e.published_gpuaddr==0x10000 && e.id==7 && proc.refs==2);
 assert(!kgsl_mem_entry_detach_process(&e) && !e.published_gpuaddr && proc.refs==1);++cases;
 printf("%d outer release scenarios passed (backend, locks and refs mocked)\n",cases);return 0;
}
