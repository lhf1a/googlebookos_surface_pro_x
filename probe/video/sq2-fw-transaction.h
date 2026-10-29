/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SQ2_FW_TRANSACTION_H
#define SQ2_FW_TRANSACTION_H
#ifdef __KERNEL__
#include <linux/errno.h>
#else
#include <errno.h>
#endif
/* The caller serializes operations. alloc must undo partial allocation on error.
 * release must not destroy this transaction object. No automatic quarantine
 * retry: an unconfirmed PAS shutdown requires a board reboot.
 */
enum sq2_fw_state { SQ2_FW_EMPTY, SQ2_FW_OWNED, SQ2_FW_TOUCHED,
                    SQ2_FW_LIVE, SQ2_FW_QUARANTINED };
struct sq2_fw_transaction {
 enum sq2_fw_state state;
 int stop_error;
};
struct sq2_fw_operations {
 int (*alloc)(void *);
 int (*load)(void *);
 int (*start)(void *);
 int (*stop)(void *);
 void (*release)(void *);
};
static int sq2_fw_stop(struct sq2_fw_transaction *t,
                       const struct sq2_fw_operations *ops, void *ctx)
{
 int ret;
 if (t->state == SQ2_FW_EMPTY)
  return 0;
 if (t->state == SQ2_FW_QUARANTINED)
  return t->stop_error;
 if (t->state != SQ2_FW_OWNED) {
  ret = ops->stop(ctx);
  if (ret) {
   t->stop_error = ret;
   t->state = SQ2_FW_QUARANTINED;
   return ret;
  }
 }
 ops->release(ctx);
 t->state = SQ2_FW_EMPTY;
 return 0;
}
static int sq2_fw_start(struct sq2_fw_transaction *t,
                        const struct sq2_fw_operations *ops, void *ctx)
{
 int ret;
 if (t->state != SQ2_FW_EMPTY)
  return -EBUSY;
 ret = ops->alloc(ctx);
 if (ret)
  return ret;
 t->state = SQ2_FW_OWNED;
 /* Even failed load may have handed metadata/memory to secure world. */
 t->state = SQ2_FW_TOUCHED;
 ret = ops->load(ctx);
 if (!ret)
  ret = ops->start(ctx);
 if (ret) {
  sq2_fw_stop(t, ops, ctx);
  return ret;
 }
 t->state = SQ2_FW_LIVE;
 return 0;
}
#endif
