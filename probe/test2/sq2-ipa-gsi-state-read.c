// SPDX-License-Identifier: GPL-2.0-only
/* Manual lab-only IPA4.1 + AP GSI status read. Never bound/autoloaded.
 * One version + one AP status + at most four AP channel context reads.
 * If AP GSI is disabled, contexts are NOT read; no ownership inferred.
 * No IPA/GSI writes, DMA, IRQ, firmware, NV, reset or remoteproc operation.
 * All Linux clock references/votes and the mapping are released in init. */
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include "sq2-ipa-version.h"
#include "sq2-ipa-gsi-read.h"

#define SQ2_IPA_VERSION_PHYS 0x01e40030UL
#define SQ2_IPA_CLOCK_INDEX 12 /* RPMH_IPA_CLK in this kernel's qcom,rpmh.h */
#define SQ2_IPA_READ_RATE 100000000UL
static bool confirm_lab;
module_param(confirm_lab, bool, 0400);
static unsigned int gsi_status, contexts[4], context_reads;
module_param(gsi_status, uint, 0444);
module_param_array(contexts, uint, NULL, 0444);
module_param(context_reads, uint, 0444);
static unsigned int version_raw;
module_param(version_raw, uint, 0444);
static unsigned long rate_before, rate_during, rate_after;
module_param(rate_before, ulong, 0444);
module_param(rate_during, ulong, 0444);
module_param(rate_after, ulong, 0444);
static bool enabled_before, enabled_after, valid;
module_param(enabled_before, bool, 0444);
module_param(enabled_after, bool, 0444);
module_param(valid, bool, 0444);

static int reject_active_ipa(struct device *dev, void *unused)
{
	int ret = 0;
	device_lock(dev);
	if (dev->driver && !strcmp(dev->driver->name, "ipa"))
		ret = -EBUSY;
	device_unlock(dev);
	return ret;
}

static int __init sq2_ipa_gsi_init(void)
{
	struct device_node *np;
	struct of_phandle_args args = { };
	struct clk *core;
	const char *model;
	void __iomem *reg, *gsi, *status;
	unsigned int i;
	bool changed_rate = false;
	int ret, restore_ret = 0;

	if (!confirm_lab) {
		pr_info("SQ2_IPA_GSI_REFUSED: confirm_lab required; no clock or MMIO accessed\n");
		return -EPERM;
	}
	np = of_find_node_by_path("/");
	if (!np)
		return -ENODEV;
	ret = of_property_read_string(np, "model", &model);
	if (!ret && strcmp(model, "Microsoft Surface Pro X (SQ2)"))
		ret = -ENODEV;
	of_node_put(np);
	if (ret)
		return ret;
	ret = bus_for_each_dev(&platform_bus_type, NULL, NULL, reject_active_ipa);
	if (ret)
		return ret;
	/* Never run concurrently with a functional Linux IPA driver. */
	np = of_find_compatible_node(NULL, NULL, "qcom,sc7180-ipa");
	if (np) {
		of_node_put(np);
		return -EBUSY;
	}
	np = of_find_compatible_node(NULL, NULL, "qcom,sc8180x-rpmh-clk");
	if (!np)
		return -ENODEV;
	args.np = np;
	args.args_count = 1;
	args.args[0] = SQ2_IPA_CLOCK_INDEX;
	core = of_clk_get_from_provider(&args);
	of_node_put(np);
	if (IS_ERR(core))
		return PTR_ERR(core);
	rate_before = clk_get_rate(core);
	enabled_before = __clk_is_enabled(core);
	/* Keep an existing rate. Only an unvoted clock gets the upstream read rate. */
	if (!rate_before) {
		ret = clk_set_rate(core, SQ2_IPA_READ_RATE);
		if (ret)
			goto put;
		changed_rate = true;
	}
	ret = clk_prepare_enable(core);
	if (ret)
		goto restore_rate;
	rate_during = clk_get_rate(core);
	if (!rate_during) {
		ret = -EIO;
		goto disable;
	}
	reg = ioremap(SQ2_IPA_VERSION_PHYS, sizeof(u32));
	if (!reg) {
		ret = -ENOMEM;
		goto disable;
	}
	version_raw = readl(reg);
	iounmap(reg);
	if (version_raw != 0x40010000) {
		ret = -EPROTONOSUPPORT;
		goto disable;
	}
	/* Fixed AP-only status/context window, no IRQ/error-clear/doorbell reads.
	 * Do not access any MODEM/Q6 execution-environment register.
	 */
	gsi = ioremap(0x01e20000, 4096);
	status = ioremap(0x01e23000, sizeof(u32));
	if (!gsi || !status) {
		if (gsi) iounmap(gsi);
		if (status) iounmap(status);
		ret = -ENOMEM;
		goto disable;
	}
	gsi_status = readl(status);
	if (gsi_status == ~0U) {
		ret = -EIO;
	} else if (gsi_status & 1) {
		for (i = 0; i < 4; i++) {
			contexts[i] = readl(gsi + sq2_gsi_ap_offset(i));
			context_reads++;
			pr_info("SQ2_IPA_GSI channel=%u raw=%08x state=%u chid=%u ee=%u event=%u\n",
				i+3, contexts[i], sq2_gsi_state(contexts[i]), sq2_gsi_chid(contexts[i]),
				sq2_gsi_ee(contexts[i]), sq2_gsi_event(contexts[i]));
			if (contexts[i] == ~0U) { ret = -EIO; break; }
		}
	}
	iounmap(status);
	iounmap(gsi);
	valid = !ret;
	pr_info("SQ2_IPA_GSI status=%08x enabled=%u context_reads=%u valid=%u NO_WRITES NO_DMA\n",
		gsi_status, gsi_status & 1, context_reads, valid);
	disable:
	clk_disable_unprepare(core);
	restore_rate:
	if (changed_rate)
		restore_ret = clk_set_rate(core, rate_before);
	rate_after = clk_get_rate(core);
	enabled_after = __clk_is_enabled(core);
	pr_info("SQ2_IPA_CLOCK_RELEASE before=%lu during=%lu after=%lu enabled=%u/%u restore=%d\n",
		rate_before, rate_during, rate_after, enabled_before, enabled_after, restore_ret);
	if (!ret && restore_ret)
		ret = restore_ret;
	put:
	clk_put(core);
	return ret;
}
static void __exit sq2_ipa_gsi_exit(void) { }
module_init(sq2_ipa_gsi_init);
module_exit(sq2_ipa_gsi_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SQ2 explicit fixed AP GSI state read, no data-plane init");
