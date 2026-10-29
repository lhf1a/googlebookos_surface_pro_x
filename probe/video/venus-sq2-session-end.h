/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef VENUS_SQ2_SESSION_END_H
#define VENUS_SQ2_SESSION_END_H
/* Caller serializes session operations. A timed-out END cannot be retried or
 * used to authorize DMA reclamation. The reply has its own completion/error. */
static int sq2_session_end(struct venus_inst *inst)
{
 int ret;
 if (test_bit(0, &inst->core->sys_error)) return -EIO;
 if (inst->sq2_end_confirmed) return 0;
 if (inst->sq2_end_sent) return -EIO;
 if (inst->state == INST_UNINIT) return 0; /* no confirmation is fabricated */
 if (inst->state < INST_INIT) return -EINVAL;
 reinit_completion(&inst->sq2_end_done);
 inst->sq2_end_error = HFI_ERR_NONE;
 inst->sq2_end_sent = true;
 ret = inst->core->ops->session_end(inst);
 if (ret) return ret;
 if (!wait_for_completion_timeout(&inst->sq2_end_done, TIMEOUT)) return -ETIMEDOUT;
 if (inst->sq2_end_error != HFI_ERR_NONE ||
     test_bit(0, &inst->core->sys_error) || inst->session_error) return -EIO;
 inst->sq2_end_confirmed = true;
 sq2_venus_scope_end(inst);
 inst->state = INST_UNINIT;
 return 0;
}
#endif
