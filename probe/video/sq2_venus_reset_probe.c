// SPDX-License-Identifier: GPL-2.0-only
/* One powered EL2 reset-state read. No PAS, firmware, reset-register writes,
 * or TZ threshold accesses. Reuses the verified clock/bridge power owner.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <asm/virt.h>
#include "sq2-venus-power.h"

static bool allow, attempted;
module_param(allow, bool, 0400);
static int result = -EINPROGRESS;
module_param(result, int, 0444);
static unsigned int reset_value = ~0U;
module_param(reset_value, uint, 0444);

static int probe(struct platform_device *pdev)
{
 struct resource *r;
 struct sq2_venus_power *power = NULL;
 void __iomem *base;
 int ret, off;
 if (attempted) return -EBUSY;
 attempted = true;
 r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
 if (!r || r->start != 0xaa00000 || resource_size(r) != 0x100000)
  return result = -EINVAL;
 if (!request_mem_region(r->start, resource_size(r), "sq2-reset-probe"))
  return result = -EBUSY;
 base = ioremap(r->start, resource_size(r));
 if (!base) {
  release_mem_region(r->start, resource_size(r));
  return result = -ENOMEM;
 }
 __module_get(THIS_MODULE);
 get_device(&pdev->dev);
 pr_info("SQ2_RESET_PROBE power_begin NO_PAS_NO_FIRMWARE\n");
 ret = sq2_venus_power_on(&pdev->dev, base, &power);
 pr_info("SQ2_RESET_PROBE power_end ret=%d\n", ret);
 if (!ret) {
  pr_info("SQ2_RESET_PROBE read_begin phys=0x0aae3000\n");
  msleep(100); /* Allow the independent O_DSYNC collector to persist BEGIN. */
  reset_value = readl(base + 0xe3000);
  pr_info("SQ2_RESET_PROBE read_end value=%08x held=%u\n",
          reset_value, !!(reset_value & BIT(4)));
  if (reset_value == ~0U) ret = -ENODEV;
 }
 off = sq2_venus_power_off(&power, false);
 result = ret ? ret : off;
 if (power) {
  /* Retain power, MMIO, device and module until reboot if restoration failed. */
  pr_err("SQ2_RESET_PROBE QUARANTINED off=%d result=%d\n", off, result);
  return 0;
 }
 iounmap(base);
 release_mem_region(r->start, resource_size(r));
 put_device(&pdev->dev);
 module_put(THIS_MODULE);
 pr_info("SQ2_RESET_PROBE released result=%d\n", result);
 return result;
}

static const struct of_device_id matches[] = {
 { .compatible = "microsoft,sq2-venus-lab" }, {}
};
static struct platform_driver driver = {
 .probe = probe,
 .driver = { .name = "sq2-venus-reset-probe", .of_match_table = matches },
};
static int __init start(void)
{
 if (!allow || !is_kernel_in_hyp_mode() ||
     !of_machine_is_compatible("microsoft,surface-pro-x") ||
     !of_machine_is_compatible("qcom,sc8180x")) return -EPERM;
 return platform_driver_register(&driver);
}
static void __exit stop(void) { platform_driver_unregister(&driver); }
module_init(start);
module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("One EL2 Venus reset-state read with bounded power ownership");
