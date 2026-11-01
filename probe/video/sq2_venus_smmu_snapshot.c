// SPDX-License-Identifier: GPL-2.0-only
/* EL2-only read of stream table entries matching the observed video FW SID.
 * Includes Qualcomm's extra groups: upstream fix 5583a55e074b explains why
 * firmware entries beyond 127 must be accounted for when running bare metal.
 */
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <asm/virt.h>
static bool allow;
module_param(allow, bool, 0400);
static int __init start(void)
{
 struct device_node *np;
 struct platform_device *pdev;
 struct resource r;
 void __iomem *gr0;
 u32 id0, count, i, smr, s2cr, mask, matches = 0;
 int ret;
 if (!allow || !is_kernel_in_hyp_mode() ||
     !of_machine_is_compatible("microsoft,surface-pro-x") ||
     !of_machine_is_compatible("qcom,sc8180x")) return -EPERM;
 np = of_find_node_by_path("/soc@0/iommu@15000000");
 if (!np) return -ENODEV;
 ret = of_address_to_resource(np, 0, &r);
 pdev = of_find_device_by_node(np);
 of_node_put(np);
 if (ret || !pdev) { if (pdev) put_device(&pdev->dev); return -ENODEV; }
 if (!pdev->dev.driver || r.start != 0x15000000 || resource_size(&r) < 4096) {
  put_device(&pdev->dev); return -EINVAL;
 }
 gr0 = ioremap(r.start, 4096);
 if (!gr0) { put_device(&pdev->dev); return -ENOMEM; }
 id0 = readl(gr0 + 0x20);
 count = id0 & 0xff;
 if (id0 == ~0U || !(id0 & BIT(27)) || !count) { ret = -EINVAL; goto out; }
 pr_info("SQ2_FW_SMMU_SNAPSHOT id0=%08x groups=%u sid=2342 NO_WRITES\n", id0, count);
 for (i = 0; i < count; i++) {
  smr = readl(gr0 + 0x800 + 4 * i);
  if (!(smr & BIT(31))) continue;
  mask = (smr >> 16) & 0x7fff;
  if (((smr ^ 0x2342) & 0x7fff & ~mask) != 0) continue;
  s2cr = readl(gr0 + 0xc00 + 4 * i);
  pr_info("SQ2_FW_SMMU_SNAPSHOT index=%u smr=%08x s2cr=%08x\n", i, smr, s2cr);
  matches++;
 }
 pr_info("SQ2_FW_SMMU_SNAPSHOT done matches=%u NO_CPU_START\n", matches);
 ret = 0;
out:
 iounmap(gr0);
 put_device(&pdev->dev);
 return ret;
}
static void __exit stop(void) {}
module_init(start);
module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read-only EL2 snapshot of all implemented SID2342 stream matches");
