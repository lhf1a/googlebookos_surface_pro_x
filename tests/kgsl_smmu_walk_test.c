/* Actual ARM LPAE geometry/unmap/free; host allocation and DMA/TLB models. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
typedef uint64_t u64;
typedef uint64_t phys_addr_t;
typedef uint64_t arm_lpae_iopte;
#define BIT(n) (1UL<<(n))
#define GENMASK_ULL(h,l) ((~0ULL>>(63-(h)))&(~0ULL<<(l)))
#define SZ_64K 65536U
#define DMA_TO_DEVICE 1
#define IO_PGTABLE_QUIRK_NO_WARN BIT(9)
#define ilog2(x) (__builtin_ctzl(x))
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define min_t(t,a,b) ((t)(a)<(t)(b)?(t)(a):(t)(b))
#define __va(x) ((void *)(uintptr_t)(x))
static unsigned warnings;
#define WARN_ON(x) ({bool b=!!(x);warnings+=b;b;})
#define WARN_ONCE(x,...) WARN_ON(x)
enum io_pgtable_fmt {ARM_64_LPAE_S1,ARM_MALI_LPAE};
struct iommu_iotlb_gather {bool queued;};
struct iommu_flush_ops {
 void (*tlb_flush_walk)(unsigned long,size_t,size_t,void *);
 int (*tlb_flush_walk_checked)(unsigned long,size_t,size_t,void *);
 void (*tlb_add_page)(struct iommu_iotlb_gather *,unsigned long,size_t,void *);
};
struct io_pgtable_cfg {bool coherent_walk;unsigned long quirks;void *iommu_dev;const struct iommu_flush_ops *tlb;};
struct io_pgtable {struct io_pgtable_cfg cfg;enum io_pgtable_fmt fmt;void *cookie;};
#include "smmu-walk-types.h"
static struct arm_lpae_io_pgtable data;
static unsigned walk_calls,legacy_calls,free_calls,add_calls,dma_syncs,restores,barriers,fail_call;
static bool last_flush_ok;
static void *tables[8];static bool live[8];static unsigned allocated;
static u64 __arm_lpae_dma_addr(void *p) {return (uintptr_t)p;}
static void dma_sync_single_for_device(void *dev,u64 addr,size_t bytes,int direction) {
 assert(dev==&data && direction==DMA_TO_DEVICE && bytes>0);dma_syncs++;
 arm_lpae_iopte *p=(void *)(uintptr_t)addr;
 if(bytes==8 && *p)restores++;
}
static void wmb(void) {barriers++;}
static bool iommu_iotlb_gather_queued(struct iommu_iotlb_gather *g) {return g->queued;}
static void __arm_lpae_free_pages(void *p,size_t size,struct io_pgtable_cfg *cfg,void *cookie) {
 assert(size==4096 && cfg==&data.iop.cfg && cookie==&data && last_flush_ok);
 unsigned i;for(i=0;i<allocated;i++)if(tables[i]==p)break;
 assert(i<allocated && live[i]);live[i]=false;free_calls++;free(p);
}
static int checked_walk(unsigned long addr,size_t size,size_t granule,void *cookie) {
 assert(cookie==&data && granule==4096 && size==ARM_LPAE_BLOCK_SIZE(data.start_level,&data));
 unsigned index=ARM_LPAE_LVL_IDX(addr,data.start_level,&data);
 assert(((arm_lpae_iopte *)data.pgd)[index]==0);walk_calls++;
 last_flush_ok=walk_calls!=fail_call;return last_flush_ok?0:-ETIMEDOUT;
}
static void legacy_walk(unsigned long addr,size_t size,size_t granule,void *cookie) {
 legacy_calls++;assert(!checked_walk(addr,size,granule,cookie));
}
static void add_page(struct iommu_iotlb_gather *g,unsigned long addr,size_t size,void *cookie) {
 assert(cookie==&data && g && !g->queued);add_calls++;
}
#include "smmu-walk-functions.h"
static struct iommu_flush_ops tlb;
static arm_lpae_iopte *table(void) {
 void *p=NULL;assert(allocated<8 && !posix_memalign(&p,4096,4096));memset(p,0,4096);
 tables[allocated]=p;live[allocated++]=true;return p;
}
static void cleanup(void) {for(unsigned i=0;i<allocated;i++)if(live[i])free(tables[i]);allocated=0;}
static void reset(bool coherent,bool checked,int level) {
 cleanup();walk_calls=legacy_calls=free_calls=add_calls=dma_syncs=restores=barriers=fail_call=warnings=0;last_flush_ok=false;
 tlb=(struct iommu_flush_ops){.tlb_flush_walk=legacy_walk,.tlb_flush_walk_checked=checked?checked_walk:NULL,.tlb_add_page=add_page};
 data=(struct arm_lpae_io_pgtable){.iop={.cfg={.coherent_walk=coherent,.iommu_dev=&data,.tlb=&tlb},.fmt=ARM_64_LPAE_S1,.cookie=&data},.pgd_bits=9,.start_level=level,.bits_per_level=9};
 data.pgd=table();
}
static arm_lpae_iopte child(unsigned index) {
 arm_lpae_iopte *p=table();p[0]=0x12345000|ARM_LPAE_PTE_TYPE_PAGE;
 arm_lpae_iopte e=paddr_to_iopte((uintptr_t)p,&data)|ARM_LPAE_PTE_TYPE_TABLE|ARM_LPAE_PTE_SW_SYNC;
 ((arm_lpae_iopte *)data.pgd)[index]=e;return e;
}
int main(void) {
 unsigned cases=0;
 for(unsigned coherent=0;coherent<2;coherent++)for(unsigned fail=0;fail<4;fail++) {
  reset(coherent,true,2);arm_lpae_iopte *root=data.pgd;root[0]=0x200000|ARM_LPAE_PTE_TYPE_BLOCK;
  arm_lpae_iopte saved[4]={root[0],child(1),child(2),child(3)};fail_call=fail;
  struct iommu_iotlb_gather gather={0};size_t block=ARM_LPAE_BLOCK_SIZE(2,&data);
  size_t done=__arm_lpae_unmap(&data,&gather,0,block,4,2,root);
  unsigned prefix=fail?fail:4;assert(done==prefix*block && free_calls==(fail?fail-1:3));
  assert(walk_calls==(fail?fail:3) && add_calls==prefix && !legacy_calls && !warnings);
  for(unsigned i=0;i<4;i++)assert(root[i]==(i<prefix?0:saved[i]));
  assert(restores==(!coherent && fail?1:0) && barriers==(fail?1:0));
  if(fail)assert(live[fail]);
  cases++;
 }
 for(unsigned coherent=0;coherent<2;coherent++) {
  reset(coherent,false,2);child(0);struct iommu_iotlb_gather g={0};
  assert(__arm_lpae_unmap(&data,&g,0,1UL<<21,1,2,data.pgd)==(1UL<<21));
  assert(free_calls==1 && legacy_calls==1 && add_calls==1);cases++;
  for(unsigned fail=0;fail<2;fail++) {
   reset(coherent,true,1);arm_lpae_iopte *root=data.pgd,*middle=table(),*leaf=table();
   leaf[9]=0x12345000|ARM_LPAE_PTE_TYPE_PAGE;
   middle[7]=paddr_to_iopte((uintptr_t)leaf,&data)|ARM_LPAE_PTE_TYPE_TABLE;
   arm_lpae_iopte saved=paddr_to_iopte((uintptr_t)middle,&data)|ARM_LPAE_PTE_TYPE_TABLE;
   root[0]=saved;fail_call=fail;
   assert(__arm_lpae_unmap(&data,NULL,0,1UL<<30,1,1,root)==(fail?0:(1UL<<30)));
   assert(free_calls==(fail?0:2) && walk_calls==1 && root[0]==(fail?saved:0));cases++;
  }
 }
 reset(false,true,2);child(0);struct iommu_iotlb_gather queued={.queued=true};
 assert(__arm_lpae_unmap(&data,&queued,0,1UL<<21,1,2,data.pgd)==(1UL<<21) && !add_calls);cases++;
 reset(false,true,1);arm_lpae_iopte *root=data.pgd;root[0]=0x40000000|ARM_LPAE_PTE_TYPE_BLOCK;
 assert(!__arm_lpae_unmap(&data,NULL,0,1UL<<21,1,1,root) && !free_calls && !walk_calls && root[0]);cases++;
 for(unsigned fail=0;fail<2;fail++) {
  reset(false,true,2);arm_lpae_iopte saved=child(0);fail_call=fail;
  int ret=arm_lpae_init_pte(&data,0,0x200000,0,2,1,data.pgd);
  assert(ret==(fail?-EINVAL:0) && free_calls==(fail?0:1));
  assert(((arm_lpae_iopte *)data.pgd)[0]==(fail?saved:(0x200000|ARM_LPAE_PTE_TYPE_BLOCK)));
  assert(warnings==fail);cases++;
 }
 cleanup();printf("PASS: %u actual ARM LPAE subtree scenarios; host pages/DMA/TLB models\n",cases);return 0;
}
