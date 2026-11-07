/* SPDX-License-Identifier: GPL-2.0-only */
/* Caller serializes transitions. A failed hardware transition is terminal;
 * busy/live sessions do not consume a transition. No retry budget or timer. */
#ifndef SQ2_VENUS_SYSTEM_PM_H
#define SQ2_VENUS_SYSTEM_PM_H
/* Positive callback result: no firmware/DMA transition occurred and IRQ
 * delivery has been restored. Never use for a partial or failed stop. */
#define SQ2_PM_STOP_UNCHANGED_BUSY 1
enum sq2_system_pm_phase {
 SQ2_PM_RUNNING, SQ2_PM_OFF, SQ2_PM_BROKEN, SQ2_PM_STOPPING, SQ2_PM_STARTING
};
struct sq2_system_pm {
 enum sq2_system_pm_phase phase;
 int error;
 unsigned long stops, starts;
};
struct sq2_system_pm_ops {
 int (*idle)(void *);
 int (*stop)(void *);
 int (*start)(void *);
};
static inline int sq2_system_pm_suspend(struct sq2_system_pm *s,
 const struct sq2_system_pm_ops *ops, void *ctx)
{
 int ret;
 if (s->phase == SQ2_PM_BROKEN) return s->error;
 if (s->phase == SQ2_PM_OFF) return 0;
 if (s->phase != SQ2_PM_RUNNING) return -EBUSY;
 ret = ops->idle(ctx);
 if (ret) return ret;
 s->phase = SQ2_PM_STOPPING;
 ret = ops->stop(ctx);
 if (ret == SQ2_PM_STOP_UNCHANGED_BUSY) {
  s->phase = SQ2_PM_RUNNING; return -EBUSY;
 }
 if (ret) {
  s->error = ret; s->phase = SQ2_PM_BROKEN; return ret;
 }
 s->stops++; s->phase = SQ2_PM_OFF;
 return 0;
}
static inline int sq2_system_pm_resume(struct sq2_system_pm *s,
 const struct sq2_system_pm_ops *ops, void *ctx)
{
 int ret;
 if (s->phase == SQ2_PM_BROKEN) return s->error;
 if (s->phase == SQ2_PM_RUNNING) return 0;
 if (s->phase != SQ2_PM_OFF) return -EBUSY;
 s->phase = SQ2_PM_STARTING;
 ret = ops->start(ctx);
 if (ret) {
  s->error = ret; s->phase = SQ2_PM_BROKEN; return ret;
 }
 s->starts++; s->phase = SQ2_PM_RUNNING;
 return 0;
}
#endif
