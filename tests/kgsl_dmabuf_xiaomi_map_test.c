/* Real payload and guard mapping; IOMMU, pages and allocations mocked. */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
typedef uint64_t u64;
typedef uint64_t phys_addr_t;
#define GFP_KERNEL 0x10
#define __GFP_ZERO 0x20
#define __GFP_NORETRY 0x40
#define __GFP_HIGHMEM 0x80
#define PAGE_SIZE 4096UL
#define PAGE_SHIFT 12
#define IOMMU_WRITE 2
#define KGSL_MEMDESC_MAP_UNCERTAIN (1U << 10)
#define ERR_PTR(n) ((void *)(intptr_t)(n))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define IS_ERR_OR_NULL(p) (!(p) || IS_ERR(p))
#define dev_err(dev, ...) ((void)(dev))
struct scatterlist { unsigned length; struct scatterlist *next; };
struct sg_table { struct scatterlist *sgl; unsigned nents, orig_nents; };
#define for_each_sg(list, s, count, i) \
 for ((i)=0,(s)=(list); (i)<(count); ++(i),(s)=(s)->next)
struct kgsl_device { void *dev; };
struct kgsl_iommu_pt { void *domain; };
struct kgsl_pagetable { void *priv; struct kgsl_device *mmu; };
#define KGSL_MMU_DEVICE(m) (m)
struct kgsl_memdesc { u64 size,gpuaddr,padding; void *pages; struct sg_table *sgt; unsigned priv; bool secure; };
static struct scatterlist segments[2];
static struct sg_table imported,temporary;
static int domain,map_calls,guard_calls,unmap_calls,alloc_calls,free_calls;
static int alloc_error,guard_fail_at,rollback_error;
static bool page_error;
static ssize_t map_result;
static u64 mapped_addr,unmap_addr,unmap_bytes;
static struct kgsl_iommu_pt iommu_pt;
static struct kgsl_pagetable pt;
static struct kgsl_device device;
static int page;
static void *kgsl_guard_page,*kgsl_secure_guard_page;
static unsigned _get_protection_flags(struct kgsl_pagetable *p,struct kgsl_memdesc *m) { assert(p==&pt && m);return 3; }
static u64 kgsl_memdesc_footprint(struct kgsl_memdesc *m) { return m->size+m->padding; }
static bool kgsl_memdesc_is_secured(struct kgsl_memdesc *m) { return m->secure; }
static void *alloc_page(unsigned flags) { return page_error?NULL:&page; }
static void *kgsl_alloc_secure_page(void) { return page_error?NULL:&page; }
static phys_addr_t page_to_phys(void *p) { assert(p==&page);return 0x100000; }
static struct sg_table *kgsl_alloc_sgt_from_pages(struct kgsl_memdesc *m) {
 assert(m->pages && !m->sgt);++alloc_calls;return alloc_error?ERR_PTR(alloc_error):&temporary;
}
static void kgsl_free_sgt(struct sg_table *s) { assert(s==&temporary && alloc_calls==1 && !alloc_error && !free_calls);++free_calls; }
static ssize_t iommu_map_sg(void *d,u64 addr,struct scatterlist *sg,unsigned nents,unsigned flags,unsigned gfp) {
 assert(d==&domain && sg==segments && nents==2 && flags==3 && gfp==GFP_KERNEL);
 assert(!map_calls);++map_calls;mapped_addr=addr;return map_result;
}
static int _iommu_unmap(struct kgsl_pagetable *p,u64 addr,u64 bytes) {
 assert(p==&pt);++unmap_calls;unmap_addr=addr;unmap_bytes=bytes;return rollback_error;
}
static size_t iommu_unmap(void *d,u64 addr,size_t bytes) {
 assert(d==&domain);return _iommu_unmap(&pt,addr,bytes)?0:bytes;
}
static int iommu_map(void *d,u64 addr,phys_addr_t phys,size_t bytes,unsigned flags,unsigned gfp) {
 assert(d==&domain && phys==0x100000 && bytes==PAGE_SIZE && flags==1 && gfp==GFP_KERNEL);
 ++guard_calls;return guard_calls==guard_fail_at?-ENOSPC:0;
}
#include "kgsl-map-functions.h"
static struct kgsl_memdesc reset(bool native) {
 segments[0]=(struct scatterlist){PAGE_SIZE*2,&segments[1]};segments[1]=(struct scatterlist){PAGE_SIZE*2,NULL};
 imported=(struct sg_table){segments,1,2};temporary=(struct sg_table){segments,2,2};
 iommu_pt.domain=&domain;pt.priv=&iommu_pt;pt.mmu=&device;
 map_calls=guard_calls=unmap_calls=alloc_calls=free_calls=0;
 alloc_error=guard_fail_at=rollback_error=0;page_error=false;
 kgsl_guard_page=kgsl_secure_guard_page=NULL;map_result=PAGE_SIZE*4;
 mapped_addr=unmap_addr=unmap_bytes=0;
 return (struct kgsl_memdesc){.size=PAGE_SIZE*4,.gpuaddr=0x10000000,.sgt=native?NULL:&imported,.pages=native?segments:NULL};
}
int main(void) {
 unsigned cases=0;
 for(int native=0;native<2;++native) for(int scenario=0;scenario<13;++scenario) {
  struct kgsl_memdesc mem=reset(native);int expected=0;bool uncertain=false;
  if(scenario==1) {map_result=-ENOMEM;expected=-ENOMEM;uncertain=true;}
  if(scenario==2) {map_result=-EIO;expected=-EIO;uncertain=true;}
  if(scenario==3) {map_result=0;expected=-EIO;}
  if(scenario==4 || scenario==5) {map_result=PAGE_SIZE;expected=-EIO;}
  if(scenario==5) {rollback_error=-ENODEV;uncertain=true;}
  if(scenario>=6 && scenario<=10) mem.padding=PAGE_SIZE*2;
  if(scenario==6 || scenario==7) {page_error=true;expected=-ENOMEM;}
  if(scenario==7) {rollback_error=-ENODEV;uncertain=true;}
  if(scenario==8 || scenario==9) {guard_fail_at=scenario==8?1:2;expected=-ENOSPC;uncertain=true;}
  if(scenario==9) rollback_error=-ENODEV;
  if(scenario==11) {mem.padding=PAGE_SIZE;mem.secure=true;}
  if(scenario==12) mem.gpuaddr=0x1ffff00000000ULL;
  assert(kgsl_iommu_map(&pt,&mem)==expected);
  assert(map_calls==1 && alloc_calls==native && free_calls==native);
  assert(!!(mem.priv & KGSL_MEMDESC_MAP_UNCERTAIN)==uncertain);
  if(scenario==4 || scenario==5) assert(unmap_calls==1 && unmap_addr==mem.gpuaddr && unmap_bytes==PAGE_SIZE);
  if(scenario>=6 && scenario<=9) assert(unmap_calls==(scenario==9?2:1) && unmap_bytes==mem.size);
  if(scenario<4 || scenario>=10) assert(!unmap_calls);
  if(scenario==10) assert(guard_calls==2);
  if(scenario==11) assert(guard_calls==1);
  if(scenario==12) assert(mapped_addr==0xffffffff00000000ULL);
  else assert(mapped_addr==mem.gpuaddr);
  assert(mem.sgt==(native?NULL:&imported));++cases;
 }
 struct kgsl_memdesc mem=reset(true);alloc_error=-ENOMEM;
 assert(kgsl_iommu_map(&pt,&mem)==-ENOMEM && !mem.priv && !map_calls && !free_calls);++cases;
 for(int scenario=0;scenario<5;++scenario) {
  mem=reset(false);
  if(scenario==0) mem.sgt=NULL;
  if(scenario==1) mem.sgt=ERR_PTR(-EFAULT);
  if(scenario==2) imported.sgl=NULL;
  if(scenario==3) imported.orig_nents=0;
  if(scenario==4) segments[1].length=0;
  assert(kgsl_iommu_map(&pt,&mem)==(scenario==1?-EFAULT:-EINVAL));
  assert(!mem.priv && !map_calls && !guard_calls && !unmap_calls && !free_calls);++cases;
 }
 printf("PASS: %u Xiaomi payload/guard cases; physical SG, hidden/failed rollback, original errno, readonly guard\n",cases);
 return 0;
}
