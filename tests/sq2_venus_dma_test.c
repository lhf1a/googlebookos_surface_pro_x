/* SPDX-License-Identifier: GPL-2.0-only */
/* Same header as Venus; inject DMA/IOMMU failures without hardware access. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
typedef uint64_t u64;
typedef uint32_t u32;
typedef uint64_t dma_addr_t;
typedef uint64_t phys_addr_t;
#define __IOMMU_DOMAIN_DMA_API 8
#define DMA_BIT_MASK(n) ((1ULL << (n)) - 1)
#define SZ_16M 0x1000000U
#define SZ_1M 0x100000U
#define PAGE_SIZE 4096U
#define IS_ALIGNED(x,a) (!((x) & ((a)-1)))
struct device { u64 *dma_mask, coherent_dma_mask, bus_dma_limit; };
struct iommu_domain { unsigned int type; };
struct iommu_fwspec { unsigned int num_ids, ids[3]; };
struct iommu_group { int unused; };
static struct iommu_domain domain = {8};
static struct iommu_fwspec spec = {3, {0x2300,0x2320,0x2340}};
static struct iommu_group group;
static int peers=1, group_refs, mask_ret, iterate_ret;
static bool no_domain, no_spec, no_group;
static int bad_page=-1, calls;
static struct iommu_domain *iommu_get_domain_for_dev(struct device *d)
{ (void)d; return no_domain ? NULL : &domain; }
static struct iommu_fwspec *dev_iommu_fwspec_get(struct device *d)
{ (void)d; return no_spec ? NULL : &spec; }
static struct iommu_group *iommu_group_get(struct device *d)
{ (void)d; if (no_group) return NULL; group_refs++; return &group; }
static void iommu_group_put(struct iommu_group *g)
{ (void)g; group_refs--; }
static int iommu_group_for_each_dev(struct iommu_group *g, void *p,
                                  int (*fn)(struct device *, void *))
{ int i; (void)g; for (i=0;i<peers;i++) fn(NULL,p); return iterate_ret; }
static int dma_set_mask_and_coherent(struct device *d,u64 mask)
{ *d->dma_mask=mask; if (mask_ret) return mask_ret; d->coherent_dma_mask=mask; return 0; }
static phys_addr_t iommu_iova_to_phys(struct iommu_domain *d,dma_addr_t a)
{ (void)d; (void)a; return calls++==bad_page ? 0 : 0x100000000ULL + (u64)calls*4096; }
#include "../probe/video/sq2-venus-dma.h"
int main(void)
{
 u64 mask=0xffffffffffULL;
 struct device d={&mask,0xfffffffffULL,0};
 struct sq2_venus_dma l={0};
 int i;
 assert(sq2_venus_dma_begin(&d,&l)==0);
 assert(mask==0xffffffffULL && d.coherent_dma_mask==mask && d.bus_dma_limit==0xbfffffffULL);
 assert(sq2_venus_dma_begin(&d,&l)==-EBUSY);
 sq2_venus_dma_restore(&d,&l);
 assert(mask==0xffffffffffULL && d.coherent_dma_mask==0xfffffffffULL && !d.bus_dma_limit);
 sq2_venus_dma_restore(&d,&l);
 d.bus_dma_limit=0xafffffff;
 assert(!sq2_venus_dma_begin(&d,&l) && d.bus_dma_limit==0xafffffff);
 sq2_venus_dma_restore(&d,&l);
 for(i=0;i<3;i++) {
  spec.ids[i] ^= 0x600000;
  assert(sq2_venus_dma_begin(&d,&l)==-ENODEV && !l.active);
  spec.ids[i] ^= 0x600000;
 }
 no_domain=true; assert(sq2_venus_dma_begin(&d,&l)==-ENODEV); no_domain=false;
 no_spec=true; assert(sq2_venus_dma_begin(&d,&l)==-ENODEV); no_spec=false;
 no_group=true; assert(sq2_venus_dma_begin(&d,&l)==-ENODEV); no_group=false;
 domain.type=0; assert(sq2_venus_dma_begin(&d,&l)==-ENODEV); domain.type=8;
 spec.num_ids=1; assert(sq2_venus_dma_begin(&d,&l)==-ENODEV); spec.num_ids=3;
 peers=2; assert(sq2_venus_dma_begin(&d,&l)==-EBUSY); peers=1;
 iterate_ret=-EIO; assert(sq2_venus_dma_begin(&d,&l)==-EIO); iterate_ret=0;
 mask_ret=-EIO; assert(sq2_venus_dma_begin(&d,&l)==-EIO && !l.active); mask_ret=0;
 assert(mask==0xffffffffffULL && d.coherent_dma_mask==0xfffffffffULL && d.bus_dma_limit==0xafffffff);
 assert(group_refs==0);
 assert(!sq2_venus_dma_check_uc(&d,0xbf000000,SZ_16M) && calls==4096);
 calls=0; assert(!sq2_venus_dma_check_uc(&d,0x25800000,SZ_16M) && calls==4096);
 calls=0; assert(sq2_venus_dma_check_uc(&d,0xbf000001,SZ_16M)==-ERANGE && !calls);
 assert(sq2_venus_dma_check_uc(&d,0x25700000,SZ_16M)==-ERANGE);
 assert(sq2_venus_dma_check_uc(&d,UINT64_MAX,SZ_16M)==-ERANGE);
 assert(sq2_venus_dma_check_uc(&d,0xbf000000,0)==-ERANGE);
 for(i=0;i<4096;i++) {
  bad_page=i; calls=0;
  assert(sq2_venus_dma_check_uc(&d,0xbf000000,SZ_16M)==-EFAULT && calls==i+1);
 }
 puts("SQ2_VENUS_DMA_PASS: exact SIDs/singleton/rollback; two 4096-page maps; all 4096 missing-page faults rejected");
 return 0;
}
