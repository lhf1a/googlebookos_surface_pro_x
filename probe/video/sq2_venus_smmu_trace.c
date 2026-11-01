// SPDX-License-Identifier: GPL-2.0-only
/* One EL1 PAS9 call, read-only stream-table comparison. No firmware calls. */
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/ptrace.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/sched.h>
#include <asm/virt.h>
#include "sq2-smmu-streams.h"

static bool allow;
module_param(allow, bool, 0400);
static void __iomem *gr0;
static struct platform_device *smmu;
static struct sq2_smmu_streams before, after;
static atomic_t captured = ATOMIC_INIT(0);

static int auth_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
 if (regs->regs[0] != 9 || strcmp(current->comm, "insmod") ||
     atomic_cmpxchg(&captured, 0, 1))
  return 1;
 sq2_smmu_streams_read(gr0, &before);
 pr_info("SQ2_VENUS_SMMU before PAS9, 128 GR0 SMR/S2CR pairs only\n");
 return 0;
}

static int auth_return(struct kretprobe_instance *ri, struct pt_regs *regs)
{
 unsigned int i, changed = 0;
 sq2_smmu_streams_read(gr0, &after);
 for (i = 0; i < SQ2_SMMU_STREAMS; i++) {
  bool delta = before.smr[i] != after.smr[i] || before.s2cr[i] != after.s2cr[i];
  changed += delta;
  if (delta || ((before.smr[i] | after.smr[i]) & BIT(31)))
   pr_info("SQ2_VENUS_SMMU index=%u before=%08x,%08x after=%08x,%08x changed=%u\n",
           i, before.smr[i], before.s2cr[i], after.smr[i], after.s2cr[i], delta);
 }
 pr_info("SQ2_VENUS_SMMU done auth=%ld changed=%u; mapping ownership not inferred\n",
         (long)regs_return_value(regs), changed);
 return 0;
}

static struct kretprobe probe = {
 .kp.symbol_name = "qcom_scm_pas_auth_and_reset",
 .entry_handler = auth_entry, .handler = auth_return, .maxactive = 4,
};

static int __init start(void)
{
 struct device_node *node;
 struct resource resource;
 int ret;
 if (!allow || is_kernel_in_hyp_mode() ||
     !of_machine_is_compatible("microsoft,surface-pro-x") ||
     !of_machine_is_compatible("qcom,sc8180x"))
  return -EPERM;
 for_each_compatible_node(node, NULL, "qcom,sc8180x-smmu-500") {
  if (!of_address_to_resource(node, 0, &resource) && resource.start == 0x15000000)
   break;
 }
 if (!node)
  return -ENODEV;
 smmu = of_find_device_by_node(node);
 of_node_put(node);
 if (!smmu)
  return -ENODEV;
 ret = -ENODEV;
 if (!smmu->dev.driver || resource_size(&resource) < SQ2_SMMU_GR0_BYTES)
  goto put;
 gr0 = ioremap(resource.start, SQ2_SMMU_GR0_BYTES);
 if (!gr0) {
  ret = -ENOMEM;
  goto put;
 }
 ret = register_kretprobe(&probe);
 if (ret) {
  iounmap(gr0);
  goto put;
 }
 pr_info("SQ2_VENUS_SMMU ready EL1, one insmod/PAS9 call, read-only GR0\n");
 return 0;
put:
 put_device(&smmu->dev);
 return ret;
}

static void __exit stop(void)
{
 unregister_kretprobe(&probe);
 pr_info("SQ2_VENUS_SMMU removed captured=%d missed=%d\n",
         atomic_read(&captured), probe.nmissed);
 iounmap(gr0);
 put_device(&smmu->dev);
}
module_init(start);
module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Single EL1 Venus PAS9 read-only SMMU stream comparison");
