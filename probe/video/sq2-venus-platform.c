// SPDX-License-Identifier: GPL-2.0-only
/* Independent core/MMIO/IRQ owner. Not yet a runnable platform backend:
 * power/reset/ICC and core.c probe/recovery/remove dispatch are still pending.
 */
#ifndef SQ2_VENUS_PLATFORM_HOST_TEST
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include "core.h"
#include "hfi.h"
#endif
#include "sq2-venus-platform.h"
#include "sq2-venus-lifetime.h"

struct sq2_venus_platform {
 struct venus_core core;
 struct sq2_venus_lifetime lifetime;
 resource_size_t start, size;
 bool region, irq_requested, irq_enabled;
};

struct venus_core *sq2_venus_platform_core(struct sq2_venus_platform *owner)
{
 return owner ? &owner->core : NULL;
}

int sq2_venus_platform_alloc(struct platform_device *pdev,
                            struct sq2_venus_platform **out)
{
 struct sq2_venus_platform *o;
 struct resource *r;
 int ret;
 if (!out || *out)
  return -EINVAL;
 r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
 if (!r || !resource_size(r))
  return -ENODEV;
 o = kzalloc(sizeof(*o), GFP_KERNEL);
 if (!o)
  return -ENOMEM;
 if (!try_module_get(THIS_MODULE)) {
  kfree(o);
  return -ENODEV;
 }
 o->core.dev = get_device(&pdev->dev);
 o->core.sq2_platform = o;
 o->start = r->start;
 o->size = resource_size(r);
 o->core.irq = platform_get_irq(pdev, 0);
 if (o->core.irq < 0) { ret = o->core.irq; goto fail; }
 if (!request_mem_region(o->start, o->size, "sq2-venus-owner")) {
  ret = -EBUSY;
  goto fail;
 }
 o->region = true;
 o->core.base = ioremap(o->start, o->size);
 if (!o->core.base) { ret = -ENOMEM; goto fail; }
 /* No handler may run before callers have initialized locks and HFI. */
 ret = request_threaded_irq(o->core.irq, hfi_isr, hfi_isr_thread,
                            IRQF_TRIGGER_HIGH | IRQF_ONESHOT | IRQF_NO_AUTOEN,
                            "sq2-venus-owner", &o->core);
 if (ret)
  goto fail;
 o->irq_requested = true;
 *out = o;
 return 0;
fail:
 if (o->core.base) iounmap(o->core.base);
 if (o->region) release_mem_region(o->start, o->size);
 put_device(o->core.dev);
 kfree(o);
 module_put(THIS_MODULE);
 return ret;
}

int sq2_venus_platform_irq(struct sq2_venus_platform *o, bool enable)
{
 if (!o || !o->irq_requested)
  return -EINVAL;
 if (enable && (o->lifetime.quarantined || !o->core.priv))
  return -EBUSY;
 if (enable == o->irq_enabled)
  return 0;
 if (enable) enable_irq(o->core.irq);
 else disable_irq(o->core.irq); /* synchronizes the threaded handler too */
 o->irq_enabled = enable;
 return 0;
}

int sq2_venus_platform_secure_begin(struct sq2_venus_platform *o)
{
 return o ? sq2_venus_lifetime_begin(&o->lifetime) : -EINVAL;
}

int sq2_venus_platform_secure_end(struct sq2_venus_platform *o, int pas_status)
{
 int ret;
 if (!o) return -EINVAL;
 ret = sq2_venus_lifetime_end(&o->lifetime, pas_status);
 if (o->lifetime.quarantined) {
  sq2_venus_platform_irq(o, false);
  dev_err(o->core.dev, "SQ2 PAS stop=%d: core/MMIO/IRQ/device/module retained until reboot\n", ret);
 }
 return ret;
}

int sq2_venus_platform_release(struct sq2_venus_platform **owner)
{
 struct sq2_venus_platform *o;
 if (!owner) return -EINVAL;
 o = *owner;
 if (!o) return 0;
 if (!sq2_venus_lifetime_releasable(&o->lifetime) ||
     o->core.priv || o->core.sq2_fw_owner)
  return -EBUSY;
 sq2_venus_platform_irq(o, false);
 free_irq(o->core.irq, &o->core);
 iounmap(o->core.base);
 release_mem_region(o->start, o->size);
 put_device(o->core.dev);
 *owner = NULL;
 kfree(o);
 module_put(THIS_MODULE);
 return 0;
}
