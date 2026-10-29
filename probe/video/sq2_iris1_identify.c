// SPDX-License-Identifier: GPL-2.0-only
/* One-shot powered identification; optional separately compiled PAS trial.
 * No HFI or IRQ. PAS trial owns all resources until confirmed shutdown.
 * Optional reset sequencing restores original reset states on exit.
 * The overlay and driver stay resident until reboot; all probe resources unwind.
 */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/reset.h>
#include <linux/delay.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#ifdef SQ2_IDENTIFY_FIRMWARE
#include "sq2-venus-fw.h"
#endif
#ifndef SQ2_IDENTIFY_EXISTING
static bool check_ref(const char *path, u32 ph);
#include "overlay.h"
#endif
static bool allow;
module_param(allow, bool, 0400);
static unsigned int power_domains;
module_param(power_domains, uint, 0400);
static unsigned int enable_clocks;
module_param(enable_clocks, uint, 0400);
static bool read_registers;
module_param(read_registers, bool, 0400);
static bool reset_bridge;
module_param(reset_bridge, bool, 0400);
static bool enable_ahb;
module_param(enable_ahb, bool, 0400);
static unsigned long core_hz;
module_param(core_hz, ulong, 0400);
static bool boot_firmware;
module_param(boot_firmware, bool, 0400);
#ifndef SQ2_IDENTIFY_EXISTING
static int overlay_id = -1;
#endif
static bool measured;
static int probe_result = -EINPROGRESS;
module_param(probe_result, int, 0444);

/* 6.3 qcom_reset_ops has no status callback. Read the same known reset bit
 * through the existing provider regmap, never guess the initial reset state.
 */
static int reset_status(struct device *dev, int index, struct reset_control *rst)
{
 static const u32 ids[] = {41, 0, 42, 43};
 static const u32 regs[] = {0xb02c, 0x850, 0xb024, 0xb028};
 struct of_phandle_args args;
 struct platform_device *provider;
 struct regmap *map;
 struct resource res;
 unsigned int value;
 int ret = reset_control_status(rst);
 if (ret != -ENOTSUPP)
  return ret;
 ret = of_parse_phandle_with_args(dev->of_node, "resets", "#reset-cells", index, &args);
 if (ret)
  return ret;
 if (args.args_count != 1 || args.args[0] != ids[index] ||
     !of_device_is_compatible(args.np, index == 1 ? "qcom,sm8150-videocc" : "qcom,gcc-sc8180x") ||
     of_address_to_resource(args.np, 0, &res) ||
     res.start != (index == 1 ? 0xab00000 : 0x100000) ||
     resource_size(&res) < regs[index] + 4) {
  of_node_put(args.np);
  return -EINVAL;
 }
 provider = of_find_device_by_node(args.np);
 of_node_put(args.np);
 if (!provider)
  return -EPROBE_DEFER;
 map = dev_get_regmap(&provider->dev, NULL);
 if (!provider->dev.driver || !map)
  ret = -ENODEV;
 else {
  ret = regmap_read(map, regs[index], &value);
  if (!ret) {
   pr_info("SQ2_IRIS1_IDENTIFY phase=reset_status_regmap index=%d reg=%x raw=%08x\n",
           index, regs[index], value);
   ret = !!(value & BIT(2));
  }
 }
 put_device(&provider->dev);
 return ret;
}

static int identify(struct platform_device *pdev)
{
 struct device *pd[3] = {NULL};
 struct clk *clks[6] = {NULL};
 struct clk *ahb = NULL;
 bool ahb_on = false, reset_changed = false;
 struct reset_control *resets[4] = {NULL};
 static const char * const reset_names[] = {"axi", "core", "axi0", "axi1"};
 int reset_states[4], nreset = 0;
 unsigned long saved_rate = 0;
 bool rate_changed = false;
#ifdef SQ2_IDENTIFY_FIRMWARE
 struct sq2_venus_fw *fw_owner = NULL;
 u32 saved_cgc = 0, saved_config = 0;
 bool config_changed = false;
#endif
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
 if (enable_ahb) {
  ahb = clk_get(&pdev->dev, "ahb");
  if (IS_ERR(ahb)) {
   ret = PTR_ERR(ahb);
   ahb = NULL;
   goto cleanup;
  }
 }
 if (reset_bridge) {
  for (i = 0; i < 4; i++) {
   resets[i] = reset_control_get_exclusive(&pdev->dev, reset_names[i]);
   if (IS_ERR(resets[i])) {
    ret = PTR_ERR(resets[i]);
    goto cleanup;
   }
   nreset++;
   reset_states[i] = reset_status(&pdev->dev, i, resets[i]);
   if (reset_states[i] < 0) {
    ret = reset_states[i];
    goto cleanup;
   }
   pr_info("SQ2_IRIS1_IDENTIFY phase=reset_initial name=%s value=%d\n",
           reset_names[i], reset_states[i]);
  }
 }
 for (i = 0; i < power_domains; i++) {
  pr_info("SQ2_IRIS1_IDENTIFY phase=pd_on_begin index=%d\n", i);
  ret = pm_runtime_resume_and_get(pd[i]);
  pr_info("SQ2_IRIS1_IDENTIFY phase=pd_on_end index=%d ret=%d\n", i, ret);
  if (ret < 0)
   goto cleanup;
  npowered++;
 }
 if (reset_bridge) {
  reset_changed = true;
  for (i = 0; i < 4; i++) {
   pr_info("SQ2_IRIS1_IDENTIFY phase=reset_assert_begin name=%s\n", reset_names[i]);
   ret = reset_control_assert(resets[i]);
   pr_info("SQ2_IRIS1_IDENTIFY phase=reset_assert_end name=%s ret=%d\n", reset_names[i], ret);
   if (ret) goto cleanup;
  }
  usleep_range(150, 250);
  for (i = 0; i < 4; i++) {
   pr_info("SQ2_IRIS1_IDENTIFY phase=reset_deassert_begin name=%s\n", reset_names[i]);
   ret = reset_control_deassert(resets[i]);
   pr_info("SQ2_IRIS1_IDENTIFY phase=reset_deassert_end name=%s ret=%d\n", reset_names[i], ret);
   if (ret) goto cleanup;
  }
 }
 for (i = 0; i < enable_clocks; i++) {
  pr_info("SQ2_IRIS1_IDENTIFY phase=clk_on_begin name=%s\n", names[i]);
  ret = clk_prepare_enable(clks[i]);
  pr_info("SQ2_IRIS1_IDENTIFY phase=clk_on_end name=%s ret=%d\n", names[i], ret);
  if (ret)
   goto cleanup;
  nenabled++;
  if (i == 2 && ahb) {
   pr_info("SQ2_IRIS1_IDENTIFY phase=ahb_on_begin\n");
   ret = clk_prepare_enable(ahb);
   pr_info("SQ2_IRIS1_IDENTIFY phase=ahb_on_end ret=%d\n", ret);
   if (ret) goto cleanup;
   ahb_on = true;
  }
 }
 if (!read_registers) {
  pr_info("SQ2_IRIS1_IDENTIFY phase=no_mmio pd=%u clk=%u NO_FIRMWARE\n",
          power_domains, enable_clocks);
  ret = 0;
  measured = true;
  goto cleanup;
 }
 if (core_hz) {
  saved_rate = clk_get_rate(clks[3]);
  rate_changed = true;
  pr_info("SQ2_IRIS1_IDENTIFY phase=rate_begin old=%lu target=%lu\n", saved_rate, core_hz);
  ret = clk_set_rate(clks[3], core_hz);
  pr_info("SQ2_IRIS1_IDENTIFY phase=rate_end ret=%d actual=%lu\n", ret, clk_get_rate(clks[3]));
  if (ret) goto cleanup;
  /* PLL fractional rounding reports 200000097Hz for the 200MHz table entry.
   * Retain a narrow 1kHz bound, and always log the actual reported rate.
   */
  if (clk_get_rate(clks[3]) > core_hz + 1000 ||
      clk_get_rate(clks[3]) + 1000 < core_hz) { ret = -ERANGE; goto cleanup; }
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
 pr_info("SQ2_IRIS1_IDENTIFY version=%08x major=%u minor=%u step=%u mask=%08x ctrl=%08x ret=%d firmware_requested=%d\n",
         version, (version >> 28) & 7, (version >> 16) & 0xfff,
         version & 0xffff, mask, status, ret, boot_firmware);
 for (i = 0; i < 6; i++)
  pr_info("SQ2_IRIS1_IDENTIFY clock=%s rate=%lu\n", names[i], clk_get_rate(clks[i]));
#ifdef SQ2_IDENTIFY_FIRMWARE
 if (boot_firmware && !ret) {
  if (version != 0x5010002f) { ret = -ENODEV; goto cleanup; }
  saved_cgc = readl(base + 0xe2010);
  saved_config = readl(base + 0xe2000);
  config_changed = true;
  writel(0, base + 0xe2010);
  writel(0, base + 0xe2000);
  pr_info("SQ2_IRIS1_IDENTIFY phase=firmware_boot_begin\n");
  ret = sq2_venus_fw_boot(&pdev->dev, &fw_owner);
  pr_info("SQ2_IRIS1_IDENTIFY phase=firmware_boot_end ret=%d owner=%d\n", ret, !!fw_owner);
  if (fw_owner) {
   int shutdown;
   if (!ret) usleep_range(20000, 25000);
   pr_info("SQ2_IRIS1_IDENTIFY phase=firmware_shutdown_begin\n");
   shutdown = sq2_venus_fw_shutdown(&fw_owner);
   pr_info("SQ2_IRIS1_IDENTIFY phase=firmware_shutdown_end ret=%d owner=%d\n", shutdown, !!fw_owner);
   if (shutdown) {
    /* No devm resources here. The globally owned firmware module pin,
     * registered overlay/devices, raw clock/reset references, powered
     * domains and MMIO reservation all survive. Never unwind after an
     * unconfirmed stop. No HFI queues or application buffers were created.
     */
    probe_result = shutdown;
    measured = true;
    pr_err("SQ2_IRIS1_IDENTIFY QUARANTINED_ALL_RESOURCES reboot_required\n");
    return 0;
   }
  }
 }
#endif
 measured = true;
cleanup:
#ifdef SQ2_IDENTIFY_FIRMWARE
 if (config_changed) {
  writel(saved_config, base + 0xe2000);
  writel(saved_cgc, base + 0xe2010);
 }
#endif
 if (rate_changed) {
  int restore = clk_set_rate(clks[3], saved_rate);
  pr_info("SQ2_IRIS1_IDENTIFY phase=rate_restore target=%lu actual=%lu ret=%d\n",
          saved_rate, clk_get_rate(clks[3]), restore);
  if (restore && !ret) ret = restore;
 }
 while (nenabled) {
  if (nenabled == 3 && ahb_on) {
   pr_info("SQ2_IRIS1_IDENTIFY phase=ahb_off_begin\n");
   clk_disable_unprepare(ahb);
   ahb_on = false;
   pr_info("SQ2_IRIS1_IDENTIFY phase=ahb_off_end\n");
  }
  --nenabled;
  pr_info("SQ2_IRIS1_IDENTIFY phase=clk_off_begin name=%s\n", names[nenabled]);
  clk_disable_unprepare(clks[nenabled]);
  pr_info("SQ2_IRIS1_IDENTIFY phase=clk_off_end name=%s\n", names[nenabled]);
 }
 if (reset_changed) {
  for (i = 0; i < 4; i++) {
   int restore = reset_states[i] ? reset_control_assert(resets[i]) :
                                   reset_control_deassert(resets[i]);
   pr_info("SQ2_IRIS1_IDENTIFY phase=reset_restore name=%s value=%d ret=%d\n",
           reset_names[i], reset_states[i], restore);
   if (restore && !ret) ret = restore;
  }
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
 if (ahb)
  clk_put(ahb);
 while (nreset)
  reset_control_put(resets[--nreset]);
 while (npd)
  dev_pm_domain_detach(pd[--npd], false);
 release_mem_region(res->start, resource_size(res));
unmap:
 iounmap(base);
 if (ret != -EPROBE_DEFER)
  probe_result = ret;
 if (ret != -EPROBE_DEFER)
  pr_info("SQ2_IRIS1_IDENTIFY resources_released ret=%d\n", ret);
 return ret;
}
static const struct of_device_id matches[] = {
 { .compatible = "microsoft,sq2-iris1-identify" }, {}
};
static struct platform_driver driver = {
 .probe = identify,
#ifdef SQ2_IDENTIFY_EXISTING
 .driver = { .name = "sq2-iris1-phase", .of_match_table = matches },
#else
 .driver = { .name = "sq2-iris1-identify", .of_match_table = matches },
#endif
};
#ifndef SQ2_IDENTIFY_EXISTING
static bool check_ref(const char *path, u32 ph)
{
 struct device_node *n = of_find_node_by_path(path);
 bool ok = n && n->phandle == ph;
 of_node_put(n);
 return ok;
}
#endif
static int __init start(void)
{
 struct device_node *n;
#ifndef SQ2_IDENTIFY_EXISTING
 int ret;
#endif
 if (!allow || !of_machine_is_compatible("microsoft,surface-pro-x"))
  return -EPERM;
 if (power_domains > 3 || enable_clocks > 6 ||
     (enable_clocks && power_domains != 3) ||
     (reset_bridge && power_domains != 3) ||
     (enable_ahb && (power_domains != 3 || enable_clocks < 3)) ||
     (read_registers && (power_domains != 3 || enable_clocks != 6 ||
                         !reset_bridge || !enable_ahb)) ||
     (core_hz && (core_hz != 200000000 || !read_registers)) ||
     (boot_firmware && (!read_registers || core_hz != 200000000)))
  return -EINVAL;
#ifndef SQ2_IDENTIFY_FIRMWARE
 if (boot_firmware)
  return -EOPNOTSUPP;
#endif
 n = of_find_node_by_path("/soc@0/clock-controller@ab00000");
#ifdef SQ2_IDENTIFY_EXISTING
 if (!n)
  return -ENODEV;
 of_node_put(n);
 return platform_driver_register(&driver);
#else
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
#endif
}
module_init(start);
#ifdef SQ2_IDENTIFY_EXISTING
static void __exit stop(void)
{
 platform_driver_unregister(&driver);
}
module_exit(stop);
#endif
MODULE_LICENSE("GPL");
#ifdef SQ2_IDENTIFY_FIRMWARE
MODULE_DESCRIPTION("SQ2 private Iris1 bring-up and opt-in PAS trial; no HFI decoder");
#else
MODULE_DESCRIPTION("SQ2 temporary power/clock identity probe; no video firmware");
#endif
