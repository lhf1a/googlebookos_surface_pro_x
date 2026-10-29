/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef VENUS_SQ2_CALLBACK_H
#define VENUS_SQ2_CALLBACK_H
#ifndef SQ2_CALLBACK_HOST_TEST
#include "hfi_msgs.h"
#endif
/* Called under core->lock, while the instance is still on core->instances. */
static inline bool sq2_callback_get(struct venus_inst *inst, u32 packet)
{
 if (!IS_SQ2_HFI(inst->core)) return true;
 if (inst->sq2_closing &&
     (packet == HFI_MSG_SESSION_EMPTY_BUFFER ||
      packet == HFI_MSG_SESSION_FILL_BUFFER || packet == HFI_MSG_EVENT_NOTIFY))
  return false;
 atomic_inc(&inst->sq2_callbacks);
 return true;
}

static inline void sq2_callback_put(struct venus_inst *inst)
{
 struct venus_core *core;
 if (!inst || !IS_SQ2_HFI(inst->core)) return;
 core = inst->core;
 /* Cache core before dropping the last instance reference. A waiter can free
  * inst immediately at zero; never use an instance-owned waitqueue here. */
 if (atomic_dec_and_test(&inst->sq2_callbacks))
  wake_up_all(&core->sys_err_done);
}

static inline void sq2_callback_drain(struct venus_inst *inst)
{
 if (IS_SQ2_HFI(inst->core))
  wait_event(inst->core->sys_err_done, !atomic_read(&inst->sq2_callbacks));
}

static inline void sq2_callback_close_begin(struct venus_inst *inst)
{
 struct venus_core *core = inst->core;
 if (!IS_SQ2_HFI(core)) return;
 mutex_lock(&core->lock);
 WRITE_ONCE(inst->sq2_closing, true);
 mutex_unlock(&core->lock);
 /* No core/instance lock held while IRQ callbacks finish. Control replies
  * remain enabled so STOP/FLUSH/RELEASE waits during VB2 teardown can finish. */
 sq2_callback_drain(inst);
}
#endif
