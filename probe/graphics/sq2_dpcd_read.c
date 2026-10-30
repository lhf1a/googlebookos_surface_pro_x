// SPDX-License-Identifier: GPL-2.0
/* One-shot native AUX reads only. No driver replacement or DPCD writes. */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <drm/display/drm_dp_aux_bus.h>
#include <drm/display/drm_dp_helper.h>

static char result[4096];
module_param_string(result, result, sizeof(result), 0400);
static size_t used;

static int read_panel(struct device *dev, void *unused)
{
 static const unsigned int offsets[] = {0x000, 0x010, 0x060, 0x070, 0x090,
                                       0x100, 0x110, 0x700, 0x2200};
 struct dp_aux_ep_device *ep;
 u8 bytes[16];
 int i, j;
 ssize_t n;
 if (!dev->bus || strcmp(dev->bus->name, "dp-aux") ||
     strcmp(dev_name(dev), "aux-ae9a000.displayport-controller"))
  return 0;
 ep = to_dp_aux_ep_dev(dev);
 if (!ep->aux || ep->aux->is_remote)
  return -ENODEV;
 for (i = 0; i < ARRAY_SIZE(offsets); i++) {
  memset(bytes, 0, sizeof(bytes));
  n = drm_dp_dpcd_read(ep->aux, offsets[i], bytes, sizeof(bytes));
  used += scnprintf(result + used, sizeof(result) - used,
                    "%05x rc=%zd:", offsets[i], n);
  if (n > 0 && n <= sizeof(bytes))
   for (j = 0; j < n; j++)
    used += scnprintf(result + used, sizeof(result) - used, " %02x", bytes[j]);
  used += scnprintf(result + used, sizeof(result) - used, "\n");
 }
 return 1;
}

static int __init sq2_dpcd_init(void)
{
 struct device *parent;
 int rc;
 parent = bus_find_device_by_name(&platform_bus_type, NULL,
                                 "ae9a000.displayport-controller");
 if (!parent)
  return -ENODEV;
 rc = device_for_each_child(parent, NULL, read_panel);
 put_device(parent);
 return rc == 1 ? 0 : (rc < 0 ? rc : -ENODEV);
}
static void __exit sq2_dpcd_exit(void) { }
module_init(sq2_dpcd_init);
module_exit(sq2_dpcd_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SQ2 one-shot read-only panel DPCD capability snapshot");
