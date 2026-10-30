/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SQ2_VENUS_RETIRE_HOST_TEST
#include <linux/module.h>
#include <linux/slab.h>
#include "core.h"
#endif
#include "sq2-venus-retire.h"

static void scope_put(struct sq2_venus_scope *scope)
{
 if (scope && refcount_dec_and_test(&scope->refs)) kfree(scope);
}

static void release_entry(struct sq2_venus_retired *entry)
{
 struct sq2_venus_scope *scope = entry->scope;
 entry->release(entry);
 scope_put(scope);
}

int sq2_venus_scope_begin(struct venus_inst *inst)
{
 struct venus_core *core = inst->core;
 struct sq2_venus_scope *old, *scope = kzalloc(sizeof(*scope), GFP_KERNEL);
 if (!scope) return -ENOMEM;
 refcount_set(&scope->refs, 1);
 mutex_lock(&core->sq2_retire_lock);
 old = inst->sq2_scope;
 if (core->sq2_dma_stopped || (old && !old->ended)) {
  mutex_unlock(&core->sq2_retire_lock); kfree(scope); return -EIO;
 }
 inst->sq2_scope = scope;
 mutex_unlock(&core->sq2_retire_lock);
 scope_put(old);
 return 0;
}
EXPORT_SYMBOL_GPL(sq2_venus_scope_begin);

/* Binding happens before a buffer address is published to HFI. Active objects
 * may survive END and be reused: only an ended old scope may be replaced. */
int sq2_venus_scope_bind(struct venus_inst *inst, struct sq2_venus_retired *entry)
{
 struct venus_core *core = inst->core;
 struct sq2_venus_scope *old, *scope;
 mutex_lock(&core->sq2_retire_lock);
 scope = inst->sq2_scope; old = entry->scope;
 if (core->sq2_dma_stopped || !scope || scope->ended ||
     (old && old != scope && !old->ended)) {
  mutex_unlock(&core->sq2_retire_lock); return -EIO;
 }
 if (old != scope) { refcount_inc(&scope->refs); entry->scope = scope; }
 mutex_unlock(&core->sq2_retire_lock);
 if (old != scope) scope_put(old);
 return 0;
}
EXPORT_SYMBOL_GPL(sq2_venus_scope_bind);

bool sq2_venus_scope_ended(struct venus_inst *inst, struct sq2_venus_retired *entry)
{
 bool ended;
 mutex_lock(&inst->core->sq2_retire_lock);
 ended = entry->scope && entry->scope->ended;
 mutex_unlock(&inst->core->sq2_retire_lock);
 return ended;
}
EXPORT_SYMBOL_GPL(sq2_venus_scope_ended);

void sq2_venus_scope_drop(struct venus_inst *inst)
{
 struct sq2_venus_scope *old;
 mutex_lock(&inst->core->sq2_retire_lock);
 old = inst->sq2_scope; inst->sq2_scope = NULL;
 mutex_unlock(&inst->core->sq2_retire_lock);
 scope_put(old);
}
EXPORT_SYMBOL_GPL(sq2_venus_scope_drop);

void sq2_venus_scope_end(struct venus_inst *inst)
{
 struct venus_core *core = inst->core;
 struct sq2_venus_retired *entry, *next;
 LIST_HEAD(done);
 mutex_lock(&core->sq2_retire_lock);
 if (inst->sq2_scope) {
  inst->sq2_scope->ended = true;
  list_for_each_entry_safe(entry, next, &core->sq2_retired, list)
   if (entry->scope == inst->sq2_scope) {
    list_del_init(&entry->list); list_add_tail(&entry->list, &done);
   }
 }
 mutex_unlock(&core->sq2_retire_lock);
 list_for_each_entry_safe(entry, next, &done, list) {
  list_del_init(&entry->list); release_entry(entry);
 }
}
EXPORT_SYMBOL_GPL(sq2_venus_scope_end);

void sq2_venus_retire_init(struct venus_core *core)
{
 INIT_LIST_HEAD(&core->sq2_retired);
 mutex_init(&core->sq2_retire_lock);
 core->sq2_dma_stopped = false;
 core->sq2_vb2_bytes = 0;
}

int sq2_venus_vb2_reserve(struct venus_core *core, unsigned long bytes)
{
 int ret = 0;
 mutex_lock(&core->sq2_retire_lock);
 if (core->sq2_dma_stopped) ret = -EIO;
 else if (!bytes || bytes > SQ2_VENUS_VB2_BUDGET ||
          core->sq2_vb2_bytes > SQ2_VENUS_VB2_BUDGET - bytes) ret = -ENOSPC;
 else core->sq2_vb2_bytes += bytes;
 mutex_unlock(&core->sq2_retire_lock);
 return ret;
}
EXPORT_SYMBOL_GPL(sq2_venus_vb2_reserve);

void sq2_venus_vb2_unreserve(struct venus_core *core, unsigned long bytes)
{
 mutex_lock(&core->sq2_retire_lock);
 if (!WARN_ON(bytes > core->sq2_vb2_bytes)) core->sq2_vb2_bytes -= bytes;
 mutex_unlock(&core->sq2_retire_lock);
}
EXPORT_SYMBOL_GPL(sq2_venus_vb2_unreserve);

void sq2_venus_retire(struct venus_core *core, struct sq2_venus_retired *entry)
{
 bool stopped;
 mutex_lock(&core->sq2_retire_lock);
 stopped = core->sq2_dma_stopped || (entry->scope && entry->scope->ended);
 if (!stopped) list_add_tail(&entry->list, &core->sq2_retired);
 mutex_unlock(&core->sq2_retire_lock);
 if (stopped) release_entry(entry);
}
EXPORT_SYMBOL_GPL(sq2_venus_retire);

void sq2_venus_retire_stopped(struct venus_core *core)
{
 LIST_HEAD(done);
 struct sq2_venus_retired *entry, *next;
 mutex_lock(&core->sq2_retire_lock);
 core->sq2_dma_stopped = true;
 list_splice_init(&core->sq2_retired, &done);
 mutex_unlock(&core->sq2_retire_lock);
 list_for_each_entry_safe(entry, next, &done, list) {
  list_del_init(&entry->list);
  release_entry(entry);
 }
}
