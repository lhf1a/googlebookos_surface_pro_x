/* SPDX-License-Identifier: GPL-2.0-only */
/* Called with powered Iris1 resources and serialized by the Venus core owner.
 * Firmware memory alone is quarantined here. Platform match remains disabled
 * until core/HFI/IRQ/devres lifetime is adapted too.
 */
#ifndef VENUS_SQ2_FIRMWARE_H
#define VENUS_SQ2_FIRMWARE_H
static int venus_sq2_boot(struct venus_core *core)
{
 const struct venus_resources *res = core->res;
 int ret, stop;

 if (!core->use_tz || !IS_IRIS1(core) || !IS_V4(core) || !res->cp_size)
  return -EINVAL;
 /* Do not repeat the known-failing second start in error recovery. */
 if (core->sq2_boot_attempted || core->sq2_fw_owner)
  return -EBUSY;
 core->sq2_boot_attempted = true;
 venus_iris1_clock_config(core);
 ret = sq2_venus_fw_boot(core->dev, &core->sq2_fw_owner, true);
 if (ret)
  return ret; /* owner can remain non-NULL on failed secure shutdown */

 ret = qcom_scm_mem_protect_video_var(res->cp_start, res->cp_size,
                                    res->cp_nonpixel_start, res->cp_nonpixel_size);
 if (ret) {
  stop = sq2_venus_fw_shutdown(&core->sq2_fw_owner);
  if (stop)
   dev_err(core->dev, "SQ2 protection=%d shutdown=%d; firmware owner retained\n",
           ret, stop);
 }
 return ret;
}

static int venus_sq2_shutdown(struct venus_core *core)
{
 /* Owner clears only after PAS success; no raw second shutdown or free. */
 return sq2_venus_fw_shutdown(&core->sq2_fw_owner);
}
#endif
