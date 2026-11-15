// SPDX-License-Identifier: GPL-2.0-only
/* Read only the ACPI-declared camera LDOs via the existing SPMI regmap. */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
static bool allow;
module_param(allow, bool, 0400);
struct search { struct device_node *np; struct device *dev; };
static int match(struct device *dev, void *data)
{
 struct search *s = data;
 if (dev->of_node == s->np) { s->dev = get_device(dev); return 1; }
 return device_for_each_child(dev, data, match);
}
static int __init start(void)
{
 static const u16 bases[] = {0x4000, 0x4d00, 0x5000};
 static const u8 offsets[] = {0, 1, 2, 3, 4, 5, 8, 0x40, 0x41, 0x45, 0x46};
 struct device_node *np;
 struct platform_device *arb;
 struct search s = {};
 struct regmap *map;
 int i, j, ret = 0;
 unsigned int v;
 if (!allow || !of_machine_is_compatible("microsoft,surface-pro-x")) return -EPERM;
 np = of_find_node_by_path("/soc@0/spmi@c440000");
 if (!np) return -ENODEV;
 arb = of_find_device_by_node(np);
 of_node_put(np);
 if (!arb) return -ENODEV;
 s.np = of_find_node_by_path("/soc@0/spmi@c440000/pmic@1");
 if (!s.np) { put_device(&arb->dev); return -ENODEV; }
 device_for_each_child(&arb->dev, &s, match);
 of_node_put(s.np);
 put_device(&arb->dev);
 if (!s.dev) return -ENODEV;
 map = dev_get_regmap(s.dev, NULL);
 if (!map) { ret = -ENODEV; goto out; }
 for (i = 0; i < ARRAY_SIZE(bases); i++) {
  for (j = 0; j < ARRAY_SIZE(offsets); j++) {
   ret = regmap_read(map, bases[i] + offsets[j], &v);
   pr_info("SPX_CAMERA_POWER_READ device=%s reg=%04x ret=%d value=%02x\n",
           dev_name(s.dev), bases[i] + offsets[j], ret, ret ? 0 : v);
   if (ret) goto out;
  }
 }
out:
 put_device(s.dev);
 return ret;
}
static void __exit stop(void) {}
module_init(start);
module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SPX camera LDO read-only SPMI probe");
