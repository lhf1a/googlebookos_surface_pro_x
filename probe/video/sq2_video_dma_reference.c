// SPDX-License-Identifier: GPL-2.0-only
/* Historical one-shot host DMA test. No VPU power/clocks/MMIO/firmware.
 * Live 6.3 overlay removal returned0 but emitted OF refcount/leak warnings.
 * Keep for evidence/reproduction only; use the resident HFI DMA path instead.
 * This source does not claim to repair that overlay lifetime defect.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/dma-mapping.h>
#include <linux/iommu.h>
#include <linux/slab.h>
#include <linux/sizes.h>
#include "reference-overlay.h"

#define DMA_BYTES (16 * SZ_1M)
#define IOVA_END 0xc0000000ULL
static bool allow;
module_param(allow, bool, 0400);
static int probe_result = -EINPROGRESS;
module_param(probe_result, int, 0400);
static int overlay_result = -EINPROGRESS;
module_param(overlay_result, int, 0400);
static unsigned int mapped_pages, unmapped_pages;
module_param(mapped_pages, uint, 0400);
module_param(unmapped_pages, uint, 0400);
static bool observed;

static int count_peer(struct device *dev, void *data)
{
 ++*(unsigned int *)data;
 return 0;
}

static int measure(struct device *dev)
{
 struct iommu_fwspec *spec = dev_iommu_fwspec_get(dev);
 struct iommu_domain *domain = iommu_get_domain_for_dev(dev);
 struct iommu_group *group;
 const u32 expected[] = { REFERENCE_IDS };
 u64 old_limit = dev->bus_dma_limit, old_mask = dma_get_mask(dev);
 u64 old_coherent = dev->coherent_dma_mask;
 unsigned int peers = 0, i;
 dma_addr_t dma;
 void *cpu;
 size_t off;
 phys_addr_t first = 0, last = 0;
 int ret;

 if (!spec || spec->num_ids != ARRAY_SIZE(expected) || !domain ||
     !(domain->type & __IOMMU_DOMAIN_DMA_API)) return -ENODEV;
 for (i = 0; i < ARRAY_SIZE(expected); i++)
  if (spec->ids[i] != expected[i]) return -EINVAL;
 group = iommu_group_get(dev);
 if (!group) return -ENODEV;
 ret = iommu_group_for_each_dev(group, &peers, count_peer);
 iommu_group_put(group);
 if (ret || peers != 1) return -EBUSY;
 ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
 if (ret) goto restore;
 dev->bus_dma_limit = min_t(u64, old_limit ?: U64_MAX, IOVA_END - 1);
 cpu = dma_alloc_coherent(dev, DMA_BYTES, &dma, GFP_KERNEL | __GFP_NOWARN);
 if (!cpu) { ret = -ENOMEM; goto restore; }
 ret = -ERANGE;
 if (dma < SZ_1M || dma > IOVA_END - DMA_BYTES || !IS_ALIGNED(dma, SZ_1M))
  goto release;
 for (off = 0; off < DMA_BYTES; off += PAGE_SIZE) {
  phys_addr_t pa = iommu_iova_to_phys(domain, dma + off);
  if (!pa || !IS_ALIGNED(pa, PAGE_SIZE)) goto release;
  if (!off) first = pa;
  last = pa;
  mapped_pages++;
 }
 memset(cpu, 0xa5, DMA_BYTES);
 ret = memchr_inv(cpu, 0xa5, DMA_BYTES) ? -EIO : 0;
 memset(cpu, 0, DMA_BYTES);
 if (memchr_inv(cpu, 0, DMA_BYTES)) ret = -EIO;
 pr_info("SQ2_DMA_REFERENCE profile=%s ids=%u first_sid=%x peers=%u domain=%x iova=%pad bytes=%u pages=%u first=%pa last=%pa cpu_ret=%d NO_VPU_DMA\n",
         REFERENCE_PROFILE, spec->num_ids, spec->ids[0], peers, domain->type,
         &dma, DMA_BYTES, mapped_pages, &first, &last, ret);
release:
 dma_free_coherent(dev, DMA_BYTES, cpu, dma);
 for (off = 0; off < DMA_BYTES; off += PAGE_SIZE)
  if (!iommu_iova_to_phys(domain, dma + off)) unmapped_pages++;
 if (unmapped_pages != DMA_BYTES / PAGE_SIZE) ret = -EIO;
 pr_info("SQ2_DMA_REFERENCE free unmapped_pages=%u expected=%lu ret=%d\n",
         unmapped_pages, DMA_BYTES / PAGE_SIZE, ret);
restore:
 dev->bus_dma_limit = old_limit;
 dma_set_mask(dev, old_mask);
 dma_set_coherent_mask(dev, old_coherent);
 return ret;
}

static int probe(struct platform_device *pdev)
{
 if (observed || !of_device_is_compatible(pdev->dev.of_node,
                                         "microsoft,sq2-video-dma-reference")) return -EBUSY;
 observed = true;
 pr_info("SQ2_DMA_REFERENCE probe profile=%s NO_POWER_NO_MMIO_NO_FIRMWARE\n", REFERENCE_PROFILE);
 probe_result = measure(&pdev->dev);
 return probe_result;
}
static const struct of_device_id matches[] = {
 { .compatible = "microsoft,sq2-video-dma-reference" }, {}
};
static struct platform_driver driver = {
 .probe = probe,
 .driver = { .name = "sq2-video-dma-reference", .of_match_table = matches,
             .probe_type = PROBE_FORCE_SYNCHRONOUS },
};

static int __init start(void)
{
 struct device_node *node;
 int id = -1, ret;
 bool registered = false;
 if (!allow || !of_machine_is_compatible("microsoft,surface-pro-x")) return -EPERM;
 node = of_find_node_by_path("/soc@0/sq2-video-dma-reference");
 if (node) { of_node_put(node); return -EEXIST; }
 node = of_find_node_by_path("/soc@0/iommu@15000000");
 if (!node) return -ENODEV;
 ret = node->phandle == REFERENCE_SMMU_PHANDLE &&
       of_device_is_compatible(node, "qcom,sc8180x-smmu-500") ? 0 : -EINVAL;
 of_node_put(node);
 if (ret) return ret;
 ret = of_overlay_fdt_apply(reference_overlay, sizeof(reference_overlay), &id);
 if (ret) return ret;
 ret = platform_driver_register(&driver);
 if (!ret) {
  registered = true;
  ret = probe_result;
 }
 if (registered) platform_driver_unregister(&driver);
 overlay_result = of_overlay_remove(&id);
 pr_info("SQ2_DMA_REFERENCE complete profile=%s probe=%d overlay_remove=%d NO_DEVICE_DMA\n",
         REFERENCE_PROFILE, probe_result, overlay_result);
 if (overlay_result) return overlay_result;
 return ret;
}
static void __exit finish(void) { }
module_init(start);
module_exit(finish);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SQ2 one-shot isolated host DMA mapping reference; no VPU activity");
