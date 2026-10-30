// SPDX-License-Identifier: GPL-2.0-only
/* Bounded read-only observation of the private insmod PAS9 trial only.
 * Never modifies SCM descriptors, saved registers or return values.
 */
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/arm-smccc.h>
#include <linux/of.h>
#include "qcom_scm.h"

static bool allow;
module_param(allow, bool, 0400);
static atomic_t outer_count = ATOMIC_INIT(0), raw_count = ATOMIC_INIT(0), conv_count = ATOMIC_INIT(0);
struct trace_data {
 void *result;
 u32 cmd;
};
static bool selected(void)
{
 return !strcmp(current->comm, "insmod");
}
static int outer_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
 struct qcom_scm_desc desc;
 struct trace_data *data = (void *)ri->data;
 if (!selected() || copy_from_kernel_nofault(&desc, (void *)regs->regs[1], sizeof(desc)) ||
     desc.svc != QCOM_SCM_SVC_PIL ||
     !(desc.args[0] == 9 || (desc.cmd == 9 && desc.args[0] == 0)) ||
     atomic_inc_return(&outer_count) > 32)
  return 1;
 data->cmd = desc.cmd;
 data->result = (void *)regs->regs[2];
 pr_info("SQ2_SCM_TRACE outer_begin cmd=%x arginfo=%x args=%llx,%llx,%llx\n",
         desc.cmd, desc.arginfo, desc.args[0], desc.args[1], desc.args[2]);
 return 0;
}
static int outer_return(struct kretprobe_instance *ri, struct pt_regs *regs)
{
 struct trace_data *data = (void *)ri->data;
 struct qcom_scm_res res = {0};
 int read = copy_from_kernel_nofault(&res, data->result, sizeof(res));
 pr_info("SQ2_SCM_TRACE outer_end cmd=%x ret=%d result_read=%d result=%llx,%llx,%llx\n",
         data->cmd, (int)regs_return_value(regs), read,
         res.result[0], res.result[1], res.result[2]);
 return 0;
}
static int raw_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
 unsigned long args[8];
 struct trace_data *data = (void *)ri->data;
 if (!selected() || copy_from_kernel_nofault(args, (void *)regs->regs[0], sizeof(args)) ||
     ((args[0] >> 8) & 0xff) != QCOM_SCM_SVC_PIL ||
     !(args[2] == 9 || ((args[0] & 0xff) == 9 && args[2] == 0)) ||
     atomic_inc_return(&raw_count) > 32)
  return 1;
 data->cmd = args[0];
 data->result = (void *)regs->regs[1];
 pr_info("SQ2_SCM_TRACE raw_begin fn=%lx arginfo=%lx args=%lx,%lx,%lx\n",
         args[0], args[1], args[2], args[3], args[4]);
 return 0;
}
static int raw_return(struct kretprobe_instance *ri, struct pt_regs *regs)
{
 struct trace_data *data = (void *)ri->data;
 struct arm_smccc_res res = {0};
 int read = copy_from_kernel_nofault(&res, data->result, sizeof(res));
 pr_info("SQ2_SCM_TRACE raw_end fn=%x read=%d a0=%lx a1=%lx a2=%lx a3=%lx\n",
         data->cmd, read, res.a0, res.a1, res.a2, res.a3);
 return 0;
}
static int convention_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
 return selected() && atomic_inc_return(&conv_count) <= 32 ? 0 : 1;
}
static int convention_return(struct kretprobe_instance *ri, struct pt_regs *regs)
{
 pr_info_ratelimited("SQ2_SCM_TRACE convention=%ld (1=legacy 2=arm32 3=arm64)\n",
                     (long)regs_return_value(regs));
 return 0;
}
static struct kretprobe probes[] = {
 { .kp.symbol_name = "qcom_scm_call", .entry_handler = outer_entry,
   .handler = outer_return, .data_size = sizeof(struct trace_data), .maxactive = 8 },
 { .kp.symbol_name = "__scm_smc_do_quirk", .entry_handler = raw_entry,
   .handler = raw_return, .data_size = sizeof(struct trace_data), .maxactive = 8 },
 { .kp.symbol_name = "__get_convention", .entry_handler = convention_entry,
   .handler = convention_return, .maxactive = 8 },
};
static int __init start(void)
{
 int i, ret;
 if (!allow || !of_machine_is_compatible("microsoft,surface-pro-x"))
  return -EPERM;
 for (i = 0; i < ARRAY_SIZE(probes); i++) {
  ret = register_kretprobe(&probes[i]);
  if (ret) {
   while (i) unregister_kretprobe(&probes[--i]);
   return ret;
  }
 }
 pr_info("SQ2_SCM_TRACE ready insmod/PAS9 or read-only MEM_AREA, 32 call limit\n");
 return 0;
}
static void __exit stop(void)
{
 int i;
 for (i = ARRAY_SIZE(probes); i-- > 0;) {
  unregister_kretprobe(&probes[i]);
  pr_info("SQ2_SCM_TRACE removed index=%d missed=%d\n", i, probes[i].nmissed);
 }
}
module_init(start);
module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Temporary PAS9 SCM ABI/result observer, no command modifications");
