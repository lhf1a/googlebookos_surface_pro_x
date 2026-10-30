/* SPDX-License-Identifier: GPL-2.0-only */
/* Include after linux/{dma-mapping,iommu,sizes}.h. No MMIO or PAS access.
 * The HFI owner keeps this lease until all its DMA allocations are released.
 * This limits allocation addresses; it does not change SCM protection ranges.
 */
#ifndef SQ2_VENUS_DMA_H
#define SQ2_VENUS_DMA_H
struct sq2_venus_dma {
 u64 mask, coherent, limit;
 bool active;
};

static int sq2_venus_count_peer(struct device *dev, void *data)
{
 unsigned int *count = data;
 (void)dev;
 (*count)++;
 return 0;
}

static void sq2_venus_dma_restore(struct device *dev, struct sq2_venus_dma *lease)
{
 if (!lease->active)
  return;
 *dev->dma_mask = lease->mask;
 dev->coherent_dma_mask = lease->coherent;
 dev->bus_dma_limit = lease->limit;
 lease->active = false;
}

static int sq2_venus_dma_begin(struct device *dev, struct sq2_venus_dma *lease)
{
 struct iommu_domain *domain = iommu_get_domain_for_dev(dev);
 struct iommu_fwspec *spec = dev_iommu_fwspec_get(dev);
 struct iommu_group *group;
 unsigned int peers = 0;
 int ret;

 if (lease->active)
  return -EBUSY;
 if (!dev->dma_mask || !domain || !(domain->type & __IOMMU_DOMAIN_DMA_API) ||
     !spec || spec->num_ids != 3 || spec->ids[0] != 0x2300 ||
     spec->ids[1] != 0x2320 || spec->ids[2] != 0x2340)
  return -ENODEV;
 group = iommu_group_get(dev);
 if (!group)
  return -ENODEV;
 ret = iommu_group_for_each_dev(group, &peers, sq2_venus_count_peer);
 iommu_group_put(group);
 if (ret || peers != 1)
  return ret ? ret : -EBUSY;

 lease->mask = *dev->dma_mask;
 lease->coherent = dev->coherent_dma_mask;
 lease->limit = dev->bus_dma_limit;
 lease->active = true;
 ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
 if (ret) {
  sq2_venus_dma_restore(dev, lease);
  return ret;
 }
 /* Preserve any stricter upstream constraint; zero means unspecified. */
 dev->bus_dma_limit = lease->limit && lease->limit < 0xbfffffffULL ?
                      lease->limit : 0xbfffffffULL;
 return 0;
}

static int sq2_venus_dma_check_uc(struct device *dev, dma_addr_t addr, u32 size)
{
 struct iommu_domain *domain = iommu_get_domain_for_dev(dev);
 u32 off;
 if (!domain || !(domain->type & __IOMMU_DOMAIN_DMA_API) ||
     size != SZ_16M || addr < 0x25800000ULL ||
     addr > 0xc0000000ULL - size || !IS_ALIGNED(addr, SZ_1M))
  return -ERANGE;
 for (off = 0; off < size; off += PAGE_SIZE) {
  phys_addr_t phys = iommu_iova_to_phys(domain, addr + off);
  if (!phys || !IS_ALIGNED(phys, PAGE_SIZE))
   return -EFAULT;
 }
 return 0;
}
#endif
