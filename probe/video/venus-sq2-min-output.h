/* SPDX-License-Identifier: GPL-2.0-only */
/* Only publish an actual initialized HFI session's input-buffer requirement.
 * Opening a device or setting its format is not an HFI SESSION_INIT. */
#ifndef VENUS_SQ2_MIN_OUTPUT_H
#define VENUS_SQ2_MIN_OUTPUT_H
static int venus_sq2_min_output_ctrl(struct venus_inst *inst,
                                    struct v4l2_ctrl *ctrl)
{
 struct hfi_buffer_requirements req;
 enum hfi_version version = inst->core->res->hfi_version;
 u32 count;
 int ret;
 if (inst->state == INST_UNINIT) return -EAGAIN;
 ret = venus_helper_get_bufreq(inst, HFI_BUFFER_INPUT, &req);
 if (ret) return ret;
 count = HFI_BUFREQ_COUNT_MIN(&req, version);
 if (count > ctrl->maximum) return -ERANGE;
 ctrl->val = count;
 return 0;
}
#endif
