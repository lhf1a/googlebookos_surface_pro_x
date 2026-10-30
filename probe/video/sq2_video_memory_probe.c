// SPDX-License-Identifier: GPL-2.0-only
/* Allocation-only test. No firmware, SCM calls, registers or hardware DMA. */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/dma-direct.h>
#include <linux/iommu.h>
#include <linux/of.h>
#include <linux/sizes.h>
#include <linux/string.h>

static bool allow;
module_param(allow, bool, 0400);

static int __init memory_probe(void)
{
 struct platform_device *pdev;
 struct device *dev;
 dma_addr_t dma;
 phys_addr_t phys;
 void *cpu;
 size_t size = 6 * SZ_1M;
 int ret;

 if (!allow || !of_machine_is_compatible("microsoft,surface-pro-x"))
  return -EPERM;
 pdev = platform_device_register_simple("sq2-video-memory-test", -1, NULL, 0);
 if (IS_ERR(pdev))
  return PTR_ERR(pdev);
 dev = &pdev->dev;
 /* This auxiliary device has no OF node, IOMMU or DMA translation. */
 if (iommu_get_domain_for_dev(dev) || get_dma_ops(dev) || dev->dma_range_map) {
  ret = -EINVAL;
  goto unregister;
 }
 ret = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(32));
 if (ret)
  goto unregister;
 cpu = dma_alloc_coherent(dev, size, &dma, GFP_KERNEL | __GFP_NOWARN);
 if (!cpu) {
  ret = -ENOMEM;
  goto unregister;
 }
 phys = dma_to_phys(dev, dma);
 ret = -ERANGE;
 if (!IS_ALIGNED(phys, SZ_1M) || phys + size > (1ULL << 32))
  goto free;
 memset(cpu, 0xa5, size);
 ret = memchr_inv(cpu, 0xa5, size) ? -EIO : 0;
 memset(cpu, 0, size);
 if (memchr_inv(cpu, 0, size))
  ret = -EIO;
free:
 pr_info("SQ2_VIDEO_MEMORY size=%zu phys=%pa dma=%pad ret=%d; freeing allocation, no firmware started\n",
         size, &phys, &dma, ret);
 dma_free_coherent(dev, size, cpu, dma);
unregister:
 platform_device_unregister(pdev);
 return ret;
}
static void __exit memory_probe_exit(void) { }
module_init(memory_probe);
module_exit(memory_probe_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SQ2 video firmware allocation feasibility test only");
