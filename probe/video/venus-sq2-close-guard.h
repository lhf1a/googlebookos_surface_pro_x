/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef VENUS_SQ2_CLOSE_GUARD_H
#define VENUS_SQ2_CLOSE_GUARD_H
/* INST_UNINIT is not proof of no firmware session: an INIT reply may time out
 * after the firmware accepted it. Scope ownership is the authoritative fact. */
static bool venus_sq2_session_unresolved(struct venus_inst *inst)
{
 struct venus_core *core = inst->core;
 bool unresolved;
 if (!IS_SQ2_HFI(core)) return false;
 mutex_lock(&core->sq2_retire_lock);
 unresolved = inst->sq2_scope && !inst->sq2_scope->ended;
 mutex_unlock(&core->sq2_retire_lock);
 return unresolved || (inst->sq2_end_sent && !inst->sq2_end_confirmed);
}

/* Before an unresolved instance can be unlinked/freed, prohibit new firmware
 * session IDs (hash32_ptr) from reusing its address. Do not fabricate END, clear
 * the error, retry PAS, or schedule recovery. DMA stays in existing retirement
 * until a separately verified complete stop/reboot. Never-init closes are safe. */
static bool venus_sq2_quarantine_unresolved(struct venus_inst *inst,
                                           int error, const char *stage)
{
 struct venus_core *core = inst->core;
 if (!venus_sq2_session_unresolved(inst)) return false;
 mutex_lock(&core->lock);
 set_bit(0, &core->sys_error);
 mutex_unlock(&core->lock);
 inst->session_error = true;
 dev_err(core->dev, "SQ2 session quarantine stage=%s error=%d; END unconfirmed, admission closed\n",
         stage, error);
 return true;
}
#endif
