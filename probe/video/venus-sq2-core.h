/* SPDX-License-Identifier: GPL-2.0-only */
/* Included in core.c after its register-offset and capability helpers.
 * Experimental, explicit opt-in. No resource match is added by this file.
 * Published V4L2 cores remain allocated until reboot on remove: open file and
 * queued buffer lifetime must not be guessed from an instantaneous list count.
 */
#ifndef VENUS_SQ2_CORE_H
#define VENUS_SQ2_CORE_H
#include "sq2-venus-platform.h"
#include "sq2-venus-retire.h"
static bool sq2_platform_allow;
module_param(sq2_platform_allow, bool, 0400);
static const struct venus_pm_ops sq2_resident_pm_ops = {0};

static int venus_sq2_stop_core(struct venus_core *core)
{
 int ret;
 sq2_venus_platform_irq(core->sq2_platform, false);
 mutex_lock(&core->lock);
 set_bit(0, &core->sys_error);
 if (core->priv) core->ops->core_deinit(core);
 core->state = CORE_UNINIT;
 ret = venus_shutdown(core);
 mutex_unlock(&core->lock);
 if (ret) return ret; /* firmware, HFI and powered platform remain owned */
 sq2_venus_retire_stopped(core);
 return sq2_venus_platform_power_off(core->sq2_platform);
}

static void venus_sq2_error(struct work_struct *work)
{
 struct venus_core *core = container_of(work, struct venus_core, work.work);
 int ret = venus_sq2_stop_core(core);
 dev_err(core->dev, "SQ2 terminal firmware error, stop=%d; no automatic firmware restart\n", ret);
 /* No stock coredump, hfi_reinit, second PAS start or scheduled retry. */
}

static int venus_sq2_remove(struct platform_device *pdev)
{
 struct venus_core *core = platform_get_drvdata(pdev);
 int ret;
 cancel_delayed_work_sync(&core->work);
 ret = venus_sq2_stop_core(core);
 if (core->sq2_children) {
  of_platform_depopulate(core->dev);
  core->sq2_children = false;
 }
 if (core->sq2_pm_enabled) {
  pm_runtime_disable(core->dev);
  core->sq2_pm_enabled = false;
 }
 /* A published core may still be referenced by open V4L2 files. Keep core,
  * v4l2_device, HFI queues and module pin even after confirmed PAS stop.
  * This is bounded lab quarantine, not completed hot-unplug support. */
 dev_warn(core->dev, "SQ2 removed, stop=%d; published owner retained until reboot\n", ret);
 return ret;
}

static int venus_sq2_probe(struct platform_device *pdev,
                           const struct venus_resources *res)
{
 struct sq2_venus_platform *owner = NULL;
 struct venus_core *core;
 int ret, stop;
 if (!sq2_platform_allow) return -EOPNOTSUPP;
 if (!res->iris1 || res->hfi_version != HFI_VERSION_4XX) return -EINVAL;
 ret = sq2_venus_platform_alloc(pdev, &owner);
 if (ret) return ret;
 core = sq2_venus_platform_core(owner);
 core->res = res;
 core->use_tz = true;
 core->pm_ops = &sq2_resident_pm_ops;
 mutex_init(&core->lock);
 mutex_init(&core->pm_lock);
 sq2_venus_retire_init(core);
 INIT_LIST_HEAD(&core->instances);
 INIT_DELAYED_WORK(&core->work, venus_sq2_error);
 init_waitqueue_head(&core->sys_err_done);
 platform_set_drvdata(pdev, core);
 venus_assign_register_offsets(core);
 ret = sq2_venus_platform_power_on(owner);
 if (ret) goto fail;
 ret = hfi_create(core, &venus_core_ops);
 if (ret) goto fail;
 ret = venus_boot(core);
 if (ret) goto fail;
 ret = hfi_core_resume(core, true);
 if (ret) goto fail;
 ret = sq2_venus_platform_irq(owner, true);
 if (ret) goto fail;
 ret = hfi_core_init(core);
 if (ret) goto fail;
 ret = venus_enumerate_codecs(core, VIDC_SESSION_TYPE_DEC);
 if (ret) goto fail;
 ret = v4l2_device_register(core->dev, &core->v4l2_dev);
 if (ret) goto fail;
 core->sq2_public = true;
 /* Keep firmware/power resident. Child PM callbacks are intentionally empty
  * while the real parent owner holds all seven clocks and three domains. */
 ret = pm_runtime_set_active(core->dev);
 if (ret) goto fail;
 pm_runtime_enable(core->dev);
 core->sq2_pm_enabled = true;
 core->sq2_children = true; /* also depopulate partial population on error */
 ret = of_platform_populate(core->dev->of_node, NULL, NULL, core->dev);
 if (ret) goto fail;
 return 0;
fail:
 cancel_delayed_work_sync(&core->work);
 stop = venus_sq2_stop_core(core);
 if (stop || core->sq2_public) {
  if (core->sq2_children) {
   of_platform_depopulate(core->dev);
   core->sq2_children = false;
  }
  if (core->sq2_pm_enabled) {
   pm_runtime_disable(core->dev);
   core->sq2_pm_enabled = false;
  }
  dev_err(core->dev, "SQ2 probe=%d stop=%d; independent owner retained\n", ret, stop);
  return ret;
 }
 if (core->priv) hfi_destroy(core);
 platform_set_drvdata(pdev, NULL);
 mutex_destroy(&core->pm_lock);
 mutex_destroy(&core->lock);
 stop = sq2_venus_platform_release(&owner);
 if (stop) dev_err(&pdev->dev, "SQ2 release=%d; retained\n", stop);
 return ret;
}
#endif
