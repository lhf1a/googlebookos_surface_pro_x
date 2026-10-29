// SPDX-License-Identifier: GPL-2.0-only
/* One-shot powered identification. No SCM, firmware, HFI, IRQ or reset writes.
 * The overlay and driver stay resident until reboot; all probe resources unwind.
 */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <linux/io.h>
static bool check_ref(const char *path, u32 ph);
#include "overlay.h"
static bool allow;
module_param(allow, bool, 0400);
static unsigned int power_domains;
module_param(power_domains, uint, 0400);
static unsigned int enable_clocks;
module_param(enable_clocks, uint, 0400);
static bool read_registers;
module_param(read_registers, bool, 0400);
static int overlay_id = -1;
static bool measured;

static int identify(struct platform_device *pdev)
{
 struct device *pd[3] = {NULL};
 struct clk *clks[6] = {NULL};
 static const char * const names[] = {"axic", "axi0", "axi1", "core", "vcodec", "cvp"};
 void __iomem *base;
 struct resource *res;
 int i, ret, npd = 0, nclk = 0, nenabled = 0, npowered = 0;
 u32 version, mask, status;

 if (measured)
  return -EBUSY;
 pr_info("SQ2_IRIS1_IDENTIFY phase=probe_begin pd=%u clk=%u read=%d\n",
         power_domains, enable_clocks, read_registers);
 res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
 if (!res || res->start != 0xaa00000 || resource_size(res) != 0x100000)
  return -EINVAL;
 base = ioremap(res->start, resource_size(res));
 if (!base)
  return -ENOMEM;
 if (!request_mem_region(res->start, resource_size(res), "sq2-iris1-identify")) {
  ret = -EBUSY;
  goto unmap;
 }
 /* Acquire everything before powering anything. */
 for (i = 0; i < 3; i++) {
  pr_info("SQ2_IRIS1_IDENTIFY phase=pd_attach_begin index=%d\n", i);
  pd[i] = dev_pm_domain_attach_by_id(&pdev->dev, i);
  if (IS_ERR_OR_NULL(pd[i])) {
   ret = pd[i] ? PTR_ERR(pd[i]) : -ENODEV;
   goto cleanup;
  }
  npd++;
 }
 for (i = 0; i < 6; i++) {
  pr_info("SQ2_IRIS1_IDENTIFY phase=clk_get_begin name=%s\n", names[i]);
  clks[i] = clk_get(&pdev->dev, names[i]);
  if (IS_ERR(clks[i])) {
   ret = PTR_ERR(clks[i]);
   goto cleanup;
  }
  pr_info("SQ2_IRIS1_IDENTIFY phase=clk_get_end name=%s\n", names[i]);
  nclk++;
 }
 for (i = 0; i < power_domains; i++) {
  pr_info("SQ2_IRIS1_IDENTIFY phase=pd_on_begin index=%d\n", i);
  ret = pm_runtime_resume_and_get(pd[i]);
  pr_info("SQ2_IRIS1_IDENTIFY phase=pd_on_end index=%d ret=%d\n", i, ret);
  if (ret < 0)
   goto cleanup;
  npowered++;
 }
 for (i = 0; i < enable_clocks; i++) {
  pr_info("SQ2_IRIS1_IDENTIFY phase=clk_on_begin name=%s\n", names[i]);
  ret = clk_prepare_enable(clks[i]);
  pr_info("SQ2_IRIS1_IDENTIFY phase=clk_on_end name=%s ret=%d\n", names[i], ret);
  if (ret)
   goto cleanup;
  nenabled++;
 }
 if (!read_registers) {
  pr_info("SQ2_IRIS1_IDENTIFY phase=no_mmio pd=%u clk=%u NO_FIRMWARE\n",
          power_domains, enable_clocks);
  ret = 0;
  measured = true;
  goto cleanup;
 }
 pr_info("SQ2_IRIS1_IDENTIFY phase=version_read_begin\n");
 version = readl(base + 0xe0000);
 pr_info("SQ2_IRIS1_IDENTIFY phase=version_read_end value=%08x\n", version);
 pr_info("SQ2_IRIS1_IDENTIFY phase=mask_read_begin\n");
 mask = readl(base + 0xe0010);
 pr_info("SQ2_IRIS1_IDENTIFY phase=mask_read_end value=%08x\n", mask);
 pr_info("SQ2_IRIS1_IDENTIFY phase=ctrl_read_begin\n");
 status = readl(base + 0xd204c);
 pr_info("SQ2_IRIS1_IDENTIFY phase=ctrl_read_end value=%08x\n", status);
 ret = (!version || version == 0xffffffff) ? -ENODEV : 0;
 pr_info("SQ2_IRIS1_IDENTIFY version=%08x major=%u minor=%u step=%u mask=%08x ctrl=%08x ret=%d NO_FIRMWARE\n",
         version, (version >> 28) & 7, (version >> 16) & 0xfff,
         version & 0xffff, mask, status, ret);
 for (i = 0; i < 6; i++)
  pr_info("SQ2_IRIS1_IDENTIFY clock=%s rate=%lu\n", names[i], clk_get_rate(clks[i]));
 measured = true;
cleanup:
 while (nenabled) {
  --nenabled;
  pr_info("SQ2_IRIS1_IDENTIFY phase=clk_off_begin name=%s\n", names[nenabled]);
  clk_disable_unprepare(clks[nenabled]);
  pr_info("SQ2_IRIS1_IDENTIFY phase=clk_off_end name=%s\n", names[nenabled]);
 }
 while (npowered) {
  int off;
  --npowered;
  pr_info("SQ2_IRIS1_IDENTIFY phase=pd_off_begin index=%d\n", npowered);
  off = pm_runtime_put_sync(pd[npowered]);
  pr_info("SQ2_IRIS1_IDENTIFY phase=pd_off_end index=%d ret=%d\n", npowered, off);
  if (off < 0) {
   pr_err("SQ2_IRIS1_IDENTIFY domain=%d put=%d\n", npowered, off);
   if (!ret) ret = off;
  }
 }
 while (nclk)
  clk_put(clks[--nclk]);
 while (npd)
  dev_pm_domain_detach(pd[--npd], false);
 release_mem_region(res->start, resource_size(res));
unmap:
 iounmap(base);
 if (ret != -EPROBE_DEFER)
  pr_info("SQ2_IRIS1_IDENTIFY resources_released ret=%d\n", ret);
 return ret;
}
static const struct of_device_id matches[] = {
 { .compatible = "microsoft,sq2-iris1-identify" }, {}
};
static struct platform_driver driver = {
 .probe = identify,
 .driver = { .name = "sq2-iris1-identify", .of_match_table = matches },
};
static bool check_ref(const char *path, u32 ph)
{
 struct device_node *n = of_find_node_by_path(path);
 bool ok = n && n->phandle == ph;
 of_node_put(n);
 return ok;
}
static int __init start(void)
{
 struct device_node *n;
 int ret;
 if (!allow || !of_machine_is_compatible("microsoft,surface-pro-x"))
  return -EPERM;
 if (power_domains > 3 || enable_clocks > 6 ||
     (enable_clocks && power_domains != 3) ||
     (read_registers && (power_domains != 3 || enable_clocks != 6)))
  return -EINVAL;
 n = of_find_node_by_path("/soc@0/clock-controller@ab00000");
 if (n) { of_node_put(n); return -EEXIST; }
 if (!check_overlay_refs())
  return -EINVAL;
 ret = platform_driver_register(&driver);
 if (ret)
  return ret;
 ret = of_overlay_fdt_apply(overlay, sizeof(overlay), &overlay_id);
 if (ret) {
  platform_driver_unregister(&driver);
  return ret;
 }
 /* Do not let an overlay containing a bound clock provider be removed.
  * This explicit lab-only pin is released by board reboot, never forced rmmod.
  */
 __module_get(THIS_MODULE);
 pr_info("SQ2_IRIS1_IDENTIFY overlay=%d resident_until_reboot\n", overlay_id);
 return 0;
}
module_init(start);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SQ2 temporary power/clock identity probe; no video firmware");
