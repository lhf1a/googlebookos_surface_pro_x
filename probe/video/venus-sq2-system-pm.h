/* SPDX-License-Identifier: GPL-2.0-only */
/* Included after venus-sq2-core.h. System sleep only: runtime PM stays resident.
 * Stop CPU and verify DMA unmap BEFORE freeing HFI; retain platform handles and
 * genpd attachments through the PM walk. Only a proven clean stop grants one
 * restart on resume. A failed stop/start remains quarantined until reboot. */
#ifndef VENUS_SQ2_SYSTEM_PM_INTEGRATION_H
#define VENUS_SQ2_SYSTEM_PM_INTEGRATION_H
static bool sq2_pm_trace;
module_param(sq2_pm_trace, bool, 0400);

/* Published SQ2 cores and this module remain pinned until reboot. Read-only
 * observations do not clear errors or request firmware/PM transitions. */
static struct venus_core *sq2_pm_status_core;
static const char *sq2_pm_stage = "probe";
static void venus_sq2_pm_publish(struct venus_core *core)
{
 WRITE_ONCE(sq2_pm_status_core, core);
}
static int venus_sq2_pm_status_get(char *buf, const struct kernel_param *kp)
{
 struct venus_core *core = READ_ONCE(sq2_pm_status_core);
 int n;
 (void)kp;
 if (!core) return scnprintf(buf, PAGE_SIZE, "unpublished\n");
 mutex_lock(&core->pm_lock);
 mutex_lock(&core->lock);
 mutex_lock(&core->sq2_retire_lock);
 n = scnprintf(buf, PAGE_SIZE,
  "stage=%s phase=%u error=%d sys_error=%d core_state=%u hfi_error=%u fw_owner=%d secure=%d no_tz=%d hfi=%d dma_stopped=%d insts=%d vb2_bytes=%llu stops=%lu starts=%lu\n",
  READ_ONCE(sq2_pm_stage), core->sq2_system_pm.phase, core->sq2_system_pm.error,
  !!test_bit(0, &core->sys_error), core->state, core->error,
  !!core->sq2_fw_owner, core->sq2_secure_active, core->sq2_no_tz_active,
  !!core->priv, core->sq2_dma_stopped, atomic_read(&core->insts_count),
  (unsigned long long)core->sq2_vb2_bytes,
  core->sq2_system_pm.stops, core->sq2_system_pm.starts);
 mutex_unlock(&core->sq2_retire_lock);
 mutex_unlock(&core->lock);
 mutex_unlock(&core->pm_lock);
 return n;
}
static const struct kernel_param_ops sq2_pm_status_ops = {
 .get = venus_sq2_pm_status_get,
};
module_param_cb(sq2_pm_status, &sq2_pm_status_ops, NULL, 0400);
static int venus_sq2_pm_result(struct venus_core *core, const char *stage, int ret)
{
 WRITE_ONCE(sq2_pm_stage, stage);
 if (ret)
  dev_err(core->dev, "SQ2_SYSTEM_PM stage=%s ret=%d phase=%u sys_error=%d core_state=%u hfi_error=%u fw_owner=%d secure=%d hfi=%d\n",
   stage, ret, core->sq2_system_pm.phase, !!test_bit(0, &core->sys_error),
   core->state, core->error, !!core->sq2_fw_owner,
   core->sq2_secure_active, !!core->priv);
 return ret;
}


static int venus_sq2_pm_idle(void *data)
{
 struct venus_core *core = data;
 int ret = 0;
 mutex_lock(&core->lock);
 if (!core->sq2_no_tz_active) ret = -EOPNOTSUPP;
 else if (test_bit(0, &core->sys_error) || core->state != CORE_INIT ||
          core->error != HFI_ERR_NONE || !core->priv ||
          !core->sq2_fw_owner || !core->sq2_secure_active) ret = -EIO;
 else if (!list_empty(&core->instances) || atomic_read(&core->insts_count))
  ret = -EBUSY;
 mutex_unlock(&core->lock);
 mutex_lock(&core->sq2_retire_lock);
 /* Retired buffers are firmware-owned until a confirmed CPU stop. They
  * must not block the very stop that makes their reclamation safe. */
 if (!ret && core->sq2_dma_stopped) ret = -EIO;
 mutex_unlock(&core->sq2_retire_lock);
 return ret;
}

static int venus_sq2_pm_stop(void *data)
{
 struct venus_core *core = data;
 int ret;
 ret = venus_sq2_pm_result(core, "stop:irq-disable",
  sq2_venus_platform_irq(core->sq2_platform, false));
 if (ret) return ret;
 cancel_delayed_work_sync(&core->work);
 ret = venus_sq2_pm_result(core, "stop:recheck-idle", venus_sq2_pm_idle(core));
 if (ret) {
  if (ret == -EBUSY) {
   ret = venus_sq2_pm_result(core, "stop:busy-irq-restore",
    sq2_venus_platform_irq(core->sq2_platform, true));
   if (!ret) return SQ2_PM_STOP_UNCHANGED_BUSY;
  }
  return ret;
 }
 /* This path asserts no-TZ reset, confirms the complete 5MiB unmap and
  * releases the firmware lease. It never requests a second PAS start. */
 ret = venus_sq2_pm_result(core, "stop:firmware-shutdown", venus_shutdown(core));
 if (ret) return ret;
 if (core->sq2_fw_owner || core->sq2_secure_active)
  return venus_sq2_pm_result(core, "stop:owner-retained", -EUCLEAN);
 ret = venus_sq2_pm_result(core, "stop:hfi-deinit", hfi_core_deinit(core, false));
 if (ret) return ret;
 hfi_destroy(core);
 sq2_venus_retire_stopped(core);
 mutex_lock(&core->sq2_retire_lock);
 ret = (core->sq2_vb2_bytes || !list_empty(&core->sq2_retired)) ? -EUCLEAN : 0;
 mutex_unlock(&core->sq2_retire_lock);
 /* Unknown live backing is retained with the stopped owner; never silently
  * discard charges or authorize a restart with unreconciled DMA ownership. */
 if (ret) return venus_sq2_pm_result(core, "stop:retired-buffers", ret);
 return venus_sq2_pm_result(core, "stop:power-quiet",
  sq2_venus_platform_system_quiet(core->sq2_platform));
}

static int venus_sq2_pm_start(void *data)
{
 struct venus_core *core = data;
 int ret, stop, quiet;
 if (core->priv || core->sq2_fw_owner || core->sq2_secure_active)
  return venus_sq2_pm_result(core, "start:owner-retained", -EUCLEAN);
 ret = venus_sq2_pm_result(core, "start:power-restore",
  sq2_venus_platform_system_restore(core->sq2_platform));
 if (ret) goto fail;
 ret = venus_sq2_pm_result(core, "start:hfi-create",
  hfi_create(core, &venus_core_ops));
 if (ret) goto fail;
 mutex_lock(&core->sq2_retire_lock);
 if (core->sq2_vb2_bytes || !list_empty(&core->sq2_retired)) {
  mutex_unlock(&core->sq2_retire_lock);
  ret = -EUCLEAN; goto fail;
 }
 core->sq2_dma_stopped = false;
 mutex_unlock(&core->sq2_retire_lock);
 /* Only SQ2_PM_OFF reaches this callback, after complete verified stop. */
 core->sq2_boot_attempted = false;
 ret = venus_sq2_pm_result(core, "start:firmware-boot",
  venus_boot(core));
 if (ret) goto fail;
 ret = venus_sq2_pm_result(core, "start:hfi-resume",
  hfi_core_resume(core, true));
 if (ret) goto fail;
 ret = venus_sq2_pm_result(core, "start:irq-enable",
  sq2_venus_platform_irq(core->sq2_platform, true));
 if (ret) goto fail;
 ret = venus_sq2_pm_result(core, "start:hfi-init",
  hfi_core_init(core));
 if (ret) goto fail;
 ret = venus_sq2_pm_result(core, "start:enumerate",
  venus_enumerate_codecs(core, VIDC_SESSION_TYPE_DEC));
 if (ret) goto fail;
 if (test_bit(0, &core->sys_error)) {
  ret = venus_sq2_pm_result(core, "start:sys-error", -EIO); goto fail;
 }
 WRITE_ONCE(sq2_pm_stage, "start:ready");
 return 0;
fail:
 sq2_venus_platform_irq(core->sq2_platform, false);
 cancel_delayed_work_sync(&core->work);
 stop = venus_shutdown(core);
 quiet = -EBUSY;
 if (!stop && !core->sq2_fw_owner && !core->sq2_secure_active) {
  if (core->priv && !hfi_core_deinit(core, false)) hfi_destroy(core);
  sq2_venus_retire_stopped(core);
  quiet = sq2_venus_platform_system_quiet(core->sq2_platform);
 }
 set_bit(0, &core->sys_error);
 dev_err(core->dev, "SQ2_SYSTEM_PM restart=%d cleanup=%d quiet=%d; no retry, owner retained\n",
         ret, stop, quiet);
 return ret;
}
static const struct sq2_system_pm_ops venus_sq2_system_ops = {
 .idle = venus_sq2_pm_idle, .stop = venus_sq2_pm_stop, .start = venus_sq2_pm_start,
};
static __maybe_unused int venus_sq2_system_suspend(struct device *dev)
{
 struct venus_core *core = dev_get_drvdata(dev);
 int ret;
 if (!IS_SQ2_HFI(core)) return pm_runtime_force_suspend(dev);
 mutex_lock(&core->pm_lock);
 ret = sq2_system_pm_suspend(&core->sq2_system_pm, &venus_sq2_system_ops, core);
 if (ret && core->sq2_system_pm.phase == SQ2_PM_BROKEN)
  set_bit(0, &core->sys_error);
 if (sq2_pm_trace)
  dev_info(dev, "SQ2_SYSTEM_PM suspend ret=%d phase=%u stops=%lu starts=%lu\n",
           ret, core->sq2_system_pm.phase,
           core->sq2_system_pm.stops, core->sq2_system_pm.starts);
 mutex_unlock(&core->pm_lock);
 return ret;
}
static __maybe_unused int venus_sq2_system_resume(struct device *dev)
{
 struct venus_core *core = dev_get_drvdata(dev);
 int ret;
 if (!IS_SQ2_HFI(core)) return pm_runtime_force_resume(dev);
 mutex_lock(&core->pm_lock);
 ret = sq2_system_pm_resume(&core->sq2_system_pm, &venus_sq2_system_ops, core);
 if (sq2_pm_trace)
  dev_info(dev, "SQ2_SYSTEM_PM resume ret=%d phase=%u stops=%lu starts=%lu\n",
           ret, core->sq2_system_pm.phase,
           core->sq2_system_pm.stops, core->sq2_system_pm.starts);
 mutex_unlock(&core->pm_lock);
 return ret;
}
#endif
