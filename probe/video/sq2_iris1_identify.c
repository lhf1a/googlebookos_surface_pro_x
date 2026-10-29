// SPDX-License-Identifier: GPL-2.0-only
/* One-shot powered identification; optional separately compiled PAS trial.
 * Optional polled HFI trial. PAS owns all resources until confirmed shutdown.
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
#include <linux/interconnect.h>
#ifdef SQ2_IDENTIFY_FIRMWARE
#include "sq2-venus-fw.h"
#endif
#ifdef SQ2_IDENTIFY_HFI
#include "sq2-iris1-hfi.h"
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
static bool pil_gap;
module_param(pil_gap, bool, 0400);
static bool inspect_gap;
module_param(inspect_gap, bool, 0400);
static bool dma_probe;
module_param(dma_probe, bool, 0400);
static bool hfi_handshake;
module_param(hfi_handshake, bool, 0400);
static bool hfi_compact_table;
module_param(hfi_compact_table, bool, 0400);
static bool hfi_empty_ping;
module_param(hfi_empty_ping, bool, 0400);
static bool protect_iova;
module_param(protect_iova, bool, 0400);
/* Allow the independent O_DSYNC collector to commit each BEGIN marker before
 * a potentially blocking bus access. Diagnostics only, never a hang timeout.
 */
static unsigned int trace_delay_ms;
module_param(trace_delay_ms, uint, 0400);
static bool vote_interconnects;
module_param(vote_interconnects, bool, 0400);
static bool icc_probe;
module_param(icc_probe, bool, 0400);
#ifdef SQ2_IDENTIFY_HFI
static struct icc_bulk_data icc_paths[] = {
 { .name = "cpu-cfg", .avg_bw = 1000, .peak_bw = 1000 },
 { .name = "video-mem", .avg_bw = 0, .peak_bw = 304000 },
 { .name = "firmware-mem", .avg_bw = 0, .peak_bw = 304000 },
};
static int icc_acquire(struct device *dev)
{
 int i, ret;
 ret = of_icc_bulk_get(dev, ARRAY_SIZE(icc_paths), icc_paths);
 if (ret) return ret;
 for (i = 0; i < ARRAY_SIZE(icc_paths); i++) {
  if (!icc_paths[i].path) { ret = -ENODEV; goto put; }
  pr_info("SQ2_IRIS1_ICC path=%s resolved=%s avg=%u peak=%u kBps\n",
          icc_paths[i].name, icc_get_name(icc_paths[i].path),
          icc_paths[i].avg_bw, icc_paths[i].peak_bw);
 }
 ret = icc_bulk_set_bw(ARRAY_SIZE(icc_paths), icc_paths);
 pr_info("SQ2_IRIS1_ICC vote_ret=%d BEFORE_POWER_RESET\n", ret);
 if (!ret) return 0;
put:
 icc_bulk_put(ARRAY_SIZE(icc_paths), icc_paths);
 return ret;
}
static int icc_release(void)
{
 int i, ret, first = 0;
 for (i = ARRAY_SIZE(icc_paths) - 1; i >= 0; i--) {
  ret = icc_set_bw(icc_paths[i].path, 0, 0);
  pr_info("SQ2_IRIS1_ICC clear=%s ret=%d\n", icc_paths[i].name, ret);
  if (ret && !first) first = ret;
 }
 icc_bulk_put(ARRAY_SIZE(icc_paths), icc_paths);
 return first;
}
#endif
static void trace_barrier(void)
{
 if (trace_delay_ms) msleep(trace_delay_ms);
}
static int bridge_pulse(struct reset_control **resets,
                        const char * const *names, const char *stage)
{
 int i, ret;
 for (i = 0; i < 4; i++) {
  pr_info("SQ2_IRIS1_IDENTIFY phase=%s_assert_begin name=%s\n", stage, names[i]);
  trace_barrier();
  ret = reset_control_assert(resets[i]);
  pr_info("SQ2_IRIS1_IDENTIFY phase=%s_assert_end name=%s ret=%d\n", stage, names[i], ret);
  if (ret) return ret;
  trace_barrier();
 }
 usleep_range(150, 250);
 for (i = 0; i < 4; i++) {
  pr_info("SQ2_IRIS1_IDENTIFY phase=%s_deassert_begin name=%s\n", stage, names[i]);
  trace_barrier();
  ret = reset_control_deassert(resets[i]);
  pr_info("SQ2_IRIS1_IDENTIFY phase=%s_deassert_end name=%s ret=%d\n", stage, names[i], ret);
  if (ret) return ret;
  trace_barrier();
 }
 return 0;
}
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
 bool firmware_attempted = false;
#endif
 static const char * const names[] = {"axic", "axi0", "axi1", "core", "vcodec", "cvp"};
 void __iomem *base;
 struct resource *res;
 int i, ret, npd = 0, nclk = 0, nenabled = 0, npowered = 0;
 u32 version, mask, status;

#ifdef SQ2_IDENTIFY_HFI
 struct sq2_hfi_transport *hfi = NULL;
 bool icc_owned = false;
 /* driver_override from an earlier private consumer must never redirect
  * this driver to the non-IOMMU identity node.
  */
 if (!of_device_is_compatible(pdev->dev.of_node, "microsoft,sq2-iris1-hfi"))
  return -ENODEV;
#endif
 if (measured)
  return -EBUSY;
#ifdef SQ2_IDENTIFY_HFI
 if (vote_interconnects) {
  ret = icc_acquire(&pdev->dev);
  if (ret) { probe_result = ret; return ret; }
  icc_owned = true;
  if (icc_probe) {
   msleep(100);
   ret = icc_release();
   measured = true;
   probe_result = ret;
   pr_info("SQ2_IRIS1_ICC probe_ret=%d NO_POWER_NO_MMIO_NO_FIRMWARE\n", ret);
   return ret;
  }
 }
 if (dma_probe) {
  ret = sq2_hfi_dma_probe(&pdev->dev);
  probe_result = ret;
  measured = true;
  return ret;
 }
#endif
 pr_info("SQ2_IRIS1_IDENTIFY phase=probe_begin pd=%u clk=%u read=%d\n",
         power_domains, enable_clocks, read_registers);
 res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
 if (!res || res->start != 0xaa00000 || resource_size(res) != 0x100000) {
#ifdef SQ2_IDENTIFY_HFI
  if (icc_owned) icc_release();
#endif
  return -EINVAL;
 }
 base = ioremap(res->start, resource_size(res));
 if (!base) {
#ifdef SQ2_IDENTIFY_HFI
  if (icc_owned) icc_release();
#endif
  return -ENOMEM;
 }
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
#ifdef SQ2_IDENTIFY_FIRMWARE
 if (inspect_gap) {
  ret = sq2_venus_fw_probe_gap();
  measured = true;
  goto cleanup;
 }
#endif
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
  trace_barrier();
  ret = pm_runtime_resume_and_get(pd[i]);
  pr_info("SQ2_IRIS1_IDENTIFY phase=pd_on_end index=%d ret=%d\n", i, ret);
  if (ret < 0)
   goto cleanup;
  npowered++;
  trace_barrier();
 }
 if (reset_bridge) {
  reset_changed = true;
  ret = bridge_pulse(resets, reset_names, "reset");
  if (ret) goto cleanup;
 }
 for (i = 0; i < enable_clocks; i++) {
  pr_info("SQ2_IRIS1_IDENTIFY phase=clk_on_begin name=%s\n", names[i]);
  trace_barrier();
  ret = clk_prepare_enable(clks[i]);
  pr_info("SQ2_IRIS1_IDENTIFY phase=clk_on_end name=%s ret=%d\n", names[i], ret);
  if (ret)
   goto cleanup;
  nenabled++;
  trace_barrier();
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
#ifdef SQ2_IDENTIFY_HFI
  if (hfi_handshake) {
   pr_info("SQ2_IRIS1_IDENTIFY phase=hfi_prepare_begin\n");
   trace_barrier();
   ret = sq2_hfi_prepare(&pdev->dev, &hfi, hfi_compact_table, hfi_empty_ping);
   pr_info("SQ2_IRIS1_IDENTIFY phase=hfi_prepare_end ret=%d\n", ret);
   trace_barrier();
   if (ret) goto cleanup;
  }
#endif
  saved_cgc = readl(base + 0xe2010);
  saved_config = readl(base + 0xe2000);
  config_changed = true;
  writel(0, base + 0xe2010);
  writel(0, base + 0xe2000);
  pr_info("SQ2_IRIS1_IDENTIFY phase=firmware_boot_begin\n");
  trace_barrier();
  firmware_attempted = true;
  ret = sq2_venus_fw_boot(&pdev->dev, &fw_owner, pil_gap);
  pr_info("SQ2_IRIS1_IDENTIFY phase=firmware_boot_end ret=%d owner=%d\n", ret, !!fw_owner);
  trace_barrier();
  if (fw_owner) {
   int shutdown;
   if (!ret) {
#ifdef SQ2_IDENTIFY_HFI
    if (hfi) ret = sq2_hfi_run(hfi, base, protect_iova, trace_delay_ms);
    else
#endif
     usleep_range(20000, 25000);
   }
   pr_info("SQ2_IRIS1_IDENTIFY phase=firmware_shutdown_begin\n");
   shutdown = sq2_venus_fw_shutdown(&fw_owner);
   pr_info("SQ2_IRIS1_IDENTIFY phase=firmware_shutdown_end ret=%d owner=%d\n", shutdown, !!fw_owner);
   if (shutdown) {
    /* No devm resources here. The globally owned firmware module pin,
     * registered overlay/devices, raw clock/reset references, powered
     * domains and MMIO reservation all survive. Never unwind after an
     * unconfirmed stop. HFI queues also survive; no application buffers exist.
     */
#ifdef SQ2_IDENTIFY_HFI
    if (hfi) sq2_hfi_quarantine(hfi);
#endif
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
#ifdef SQ2_IDENTIFY_HFI
 if (hfi) {
  int finish = sq2_hfi_finish(hfi);
  if (!ret) ret = finish;
 }
#endif
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
#ifdef SQ2_IDENTIFY_FIRMWARE
 /* Pinned downstream __venus_power_off(true) pulses the bridge after clocks
  * are disabled for VERSION_HANA=0x50100000. Merely restoring deasserted
  * initial bits omits this recovery. Only do it after a confirmed PAS stop;
  * the shutdown-failure path above keeps all resources and never gets here.
  * Domains and interconnect votes remain held throughout the pulse.
  */
 if (firmware_attempted && reset_changed && (version & ~0xffffU) == 0x50100000) {
  int recover = bridge_pulse(resets, reset_names, "post_reset");
  pr_info("SQ2_IRIS1_IDENTIFY phase=post_reset_end ret=%d hfi_ret=%d\n", recover, ret);
  if (recover) ret = recover;
 }
#endif
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
#ifdef SQ2_IDENTIFY_HFI
 if (icc_owned) {
  int clear = icc_release();
  if (!ret) ret = clear;
 }
#endif
 if (ret != -EPROBE_DEFER)
  probe_result = ret;
 if (ret != -EPROBE_DEFER)
  pr_info("SQ2_IRIS1_IDENTIFY resources_released ret=%d\n", ret);
 return ret;
}
static const struct of_device_id matches[] = {
#ifdef SQ2_IDENTIFY_HFI
 { .compatible = "microsoft,sq2-iris1-hfi" }, {}
#else
 { .compatible = "microsoft,sq2-iris1-identify" }, {}
#endif
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
 if ((pil_gap && !boot_firmware && !inspect_gap) ||
     (inspect_gap && (!pil_gap || boot_firmware || read_registers ||
                      power_domains || enable_clocks || reset_bridge || enable_ahb || core_hz)))
  return -EINVAL;
 if (dma_probe && (power_domains || enable_clocks || reset_bridge ||
                   read_registers || enable_ahb || core_hz || boot_firmware || pil_gap || inspect_gap || hfi_handshake))
  return -EINVAL;
 if (hfi_handshake && (!boot_firmware || !pil_gap || !vote_interconnects)) return -EINVAL;
 if (protect_iova && !hfi_handshake) return -EINVAL;
 if (hfi_compact_table && !hfi_handshake) return -EINVAL;
 if (hfi_empty_ping && !hfi_handshake) return -EINVAL;
 if (trace_delay_ms > 100 || (trace_delay_ms && !reset_bridge)) return -EINVAL;
 if (icc_probe && (!vote_interconnects || power_domains || enable_clocks ||
                   reset_bridge || enable_ahb || read_registers || core_hz ||
                   boot_firmware || pil_gap || inspect_gap || dma_probe || hfi_handshake))
  return -EINVAL;
 if (vote_interconnects && (dma_probe || inspect_gap)) return -EINVAL;
#ifndef SQ2_IDENTIFY_HFI
 if (dma_probe || hfi_handshake || vote_interconnects || icc_probe) return -EOPNOTSUPP;
#endif
#ifndef SQ2_IDENTIFY_FIRMWARE
 if (boot_firmware || pil_gap || inspect_gap)
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
