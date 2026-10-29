// SPDX-License-Identifier: GPL-2.0-only
/* Read-only TZ_PIL_GET_MEM_AREA (PIL cmd9, spare0, arginfo1).
 * Linux 6.3 has no exported wrapper. Resolve only the pinned private SCM call
 * entry, then unregister the address lookup probe before invoking it normally.
 * The SCM driver performs convention selection, locking, waitq and retries.
 * No load, memory assignment, authentication, register write or firmware start.
 */
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/arm-smccc.h>
#include "qcom_scm.h"
#include "sq2-pil-region.h"
#ifndef SQ2_PIL_QUERY_LIBRARY
static bool allow;
module_param(allow, bool, 0400);
#endif
int sq2_pil_get_bounds(phys_addr_t *start, phys_addr_t *end)
{
 struct device_node *node;
 struct platform_device *pdev;
 struct kprobe lookup = { .symbol_name = "qcom_scm_call" };
 int (*call)(struct device *, const struct qcom_scm_desc *, struct qcom_scm_res *);
 struct qcom_scm_desc desc = {
  .svc = QCOM_SCM_SVC_PIL, .cmd = 9, .arginfo = QCOM_SCM_ARGS(1),
  .args = {0}, .owner = ARM_SMCCC_OWNER_SIP,
 };
 struct qcom_scm_res result = {0};
 int ret;
 if (!of_machine_is_compatible("microsoft,surface-pro-x"))
  return -EPERM;
 if (!qcom_scm_is_available())
  return -EPROBE_DEFER;
 node = of_find_node_by_path("/firmware/scm");
 if (!node)
  return -ENODEV;
 if (!of_device_is_compatible(node, "qcom,scm-sc8180x")) {
  of_node_put(node);
  return -EINVAL;
 }
 pdev = of_find_device_by_node(node);
 of_node_put(node);
 if (!pdev)
  return -ENODEV;
 if (!pdev->dev.driver) {
  ret = -ENODEV;
  goto put;
 }
 ret = register_kprobe(&lookup);
 if (ret)
  goto put;
 call = (void *)lookup.addr;
 unregister_kprobe(&lookup);
 pr_info("SQ2_PIL_MEM_QUERY begin cmd=9 spare=0 READ_ONLY\n");
 ret = call(&pdev->dev, &desc, &result);
 pr_info("SQ2_PIL_MEM_QUERY end ret=%d result=%llx,%llx,%llx; not an ownership grant\n",
         ret, result.result[0], result.result[1], result.result[2]);
 if (!ret) {
  if (!result.result[0] || result.result[1] <= result.result[0] ||
      result.result[1] > (1ULL << 32))
   ret = -ERANGE;
  else {
   *start = result.result[0];
   *end = result.result[1];
  }
 }
put:
 put_device(&pdev->dev);
 return ret;
}
#ifndef SQ2_PIL_QUERY_LIBRARY
static int __init query(void)
{
 phys_addr_t start, end;
 if (!allow) return -EPERM;
 return sq2_pil_get_bounds(&start, &end);
}
static void __exit done(void) { }
module_init(query);
module_exit(done);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read-only documented PIL load-region query for SQ2 lab");
#endif
