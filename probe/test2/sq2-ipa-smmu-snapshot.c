// SPDX-License-Identifier: GPL-2.0-only
/* Reuse the proven EL2 GR0 snapshot method for the original IORT IPA range.
 * No context-bank/TZ/page-table access, register writes, DMA or firmware call. */
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <asm/virt.h>
static bool allow;
module_param(allow, bool, 0400);
static unsigned int id0, scr0, groups, matches[3];
module_param(id0, uint, 0444);
module_param(scr0, uint, 0444);
module_param(groups, uint, 0444);
module_param_array(matches, uint, NULL, 0444);
static int __init start(void)
{
	struct device_node *np;
	struct platform_device *pdev;
	struct resource r;
	void __iomem *gr0;
	u32 i, j, smr, s2cr, mask;
	int ret;
	if (!allow || !is_kernel_in_hyp_mode() ||
	    !of_machine_is_compatible("microsoft,surface-pro-x") ||
	    !of_machine_is_compatible("qcom,sc8180x")) {
		pr_info("SQ2_IPA_SMMU_REFUSED: explicit EL2 lab allow required; no MMIO\n");
		return -EPERM;
	}
	np = of_find_node_by_path("/soc@0/iommu@15000000");
	if (!np) return -ENODEV;
	ret = of_address_to_resource(np, 0, &r);
	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (ret || !pdev) {
		if (pdev) put_device(&pdev->dev);
		return -ENODEV;
	}
	if (!pdev->dev.driver || r.start != 0x15000000 || resource_size(&r) < 4096) {
		ret = -EINVAL; goto put;
	}
	gr0 = ioremap(r.start, 4096);
	if (!gr0) { ret = -ENOMEM; goto put; }
	id0 = readl(gr0 + 0x20);
	scr0 = readl(gr0);
	groups = id0 & 0xff;
	if (id0 == ~0U || !(id0 & BIT(27)) || !groups) { ret = -EINVAL; goto unmap; }
	/* This bounded matcher implements the observed 15-bit, VALID-in-SMR form. */
	if ((id0 & BIT(8)) || (scr0 & BIT(3))) { ret = -EOPNOTSUPP; goto unmap; }
	pr_info("SQ2_IPA_SMMU id0=%08x scr0=%08x groups=%u NO_WRITES\n", id0, scr0, groups);
	for (i = 0; i < groups; i++) {
		smr = readl(gr0 + 0x800 + 4 * i);
		if (!(smr & BIT(31))) continue;
		mask = (smr >> 16) & 0x7fff;
		for (j = 0; j < 3; j++) {
			u32 sid = 0x520 + j;
			if (((smr ^ sid) & 0x7fff & ~mask) != 0) continue;
			s2cr = readl(gr0 + 0xc00 + 4 * i);
			pr_info("SQ2_IPA_SMMU sid=%03x index=%u smr=%08x s2cr=%08x type=%u cb=%u\n",
				sid, i, smr, s2cr, (s2cr >> 16) & 3, s2cr & 0xff);
			matches[j]++;
		}
	}
	pr_info("SQ2_IPA_SMMU done matches=%u,%u,%u ownership_not_inferred NO_DMA\n", matches[0], matches[1], matches[2]);
	ret = 0;
unmap:
	iounmap(gr0);
put:
	put_device(&pdev->dev);
	return ret;
}
static void __exit stop(void) { }
module_init(start);
module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read-only SQ2 EL2 IPA IORT SID-range GR0 snapshot");
