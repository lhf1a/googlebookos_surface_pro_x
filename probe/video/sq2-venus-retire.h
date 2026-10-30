/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SQ2_VENUS_RETIRE_H
#define SQ2_VENUS_RETIRE_H
#ifndef SQ2_VENUS_RETIRE_HOST_TEST
#include <linux/list.h>
#include <linux/refcount.h>
#endif
struct venus_core;
struct venus_inst;
struct sq2_venus_retired;
struct sq2_venus_scope {
 refcount_t refs;
 bool ended; /* protected by core retirement lock; never reset */
};
int sq2_venus_scope_begin(struct venus_inst *);
int sq2_venus_scope_bind(struct venus_inst *, struct sq2_venus_retired *);
bool sq2_venus_scope_ended(struct venus_inst *, struct sq2_venus_retired *);
void sq2_venus_scope_end(struct venus_inst *);
void sq2_venus_scope_drop(struct venus_inst *);
/* Lab memory containment, not a firmware capability/maximum frame size. Counts
 * live AND retired VB2, DPB and internal backing until actual release.
 * The vb2-named API is retained for existing callers; the budget is shared. */
#define SQ2_VENUS_VB2_BUDGET (1UL << 30)
int sq2_venus_vb2_reserve(struct venus_core *, unsigned long bytes);
void sq2_venus_vb2_unreserve(struct venus_core *, unsigned long bytes);
struct sq2_venus_retired {
 struct sq2_venus_scope *scope;
 struct list_head list;
 void (*release)(struct sq2_venus_retired *);
};
/* Caller must not hold core->lock. Entries own every object used by release,
 * including the callback module. No reference to an expired vb2_buffer/inst. */
void sq2_venus_retire(struct venus_core *, struct sq2_venus_retired *);
void sq2_venus_retire_init(struct venus_core *);
/* Only after IRQ synchronization and a successful PAS stop. */
void sq2_venus_retire_stopped(struct venus_core *);
#endif
