/* SPDX-License-Identifier: GPL-2.0-only */
/* Host ASan/UBSan test of actual venus-sq2-system-pm.h runtime PM + s2idle
 * state machine, lock hierarchy, cached caps export, and quarantine paths. */
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SQ2_VENUS_SYSTEM_PM
#define __maybe_unused
#define PAGE_SIZE 4096
#define CORE_UNINIT 0
#define CORE_INIT 1
#define HFI_ERR_NONE 0
#define VIDC_SESSION_TYPE_DEC 2
#define HFI_VIDEO_CODEC_H264 1
#define HFI_VIDEO_CODEC_HEVC 2
#define HFI_VIDEO_CODEC_VP8 3
#define HFI_VIDEO_CODEC_VP9 4
#define HFI_VIDEO_CODEC_MPEG2 5
#define HFI_VIDEO_CODEC_MPEG1 6
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x, v) ((x) = (v))
#define module_param(a, b, c)
#define module_param_cb(a, b, c, d)

typedef uint32_t u32;
typedef struct { int v; } atomic_t;
static int atomic_read(const atomic_t *a) { return a->v; }

/* Strict lock-order verifier:
 *   level 1: pm_lock
 *   level 3: sq2_pm_mutex
 *   level 4: core->lock
 *   level 5: sq2_retire_lock
 */
struct mutex { int level; bool held; };
static int held_mask;
static bool forbid_pm_lock;
static void mutex_lock(struct mutex *m) {
 assert(!m->held);
 if (forbid_pm_lock) assert(m->level != 1);
 /* Never acquire an outer lock while holding a same-or-inner lock. */
 assert((held_mask >> m->level) == 0);
 m->held = true;
 held_mask |= (1 << m->level);
}
static void mutex_unlock(struct mutex *m) {
 assert(m->held);
 m->held = false;
 held_mask &= ~(1 << m->level);
}

struct list_head { struct list_head *next, *prev; };
static bool list_empty(const struct list_head *h) { return h->next == h; }
static void init_list(struct list_head *h) { h->next = h; h->prev = h; }

struct delayed_work { int unused; };
struct device { void *drvdata; };
static void *dev_get_drvdata(struct device *d) { return d->drvdata; }
static void dev_err(struct device *d, const char *fmt, ...) { (void)d; (void)fmt; }

struct kernel_param { void *arg; };
struct kernel_param_ops { int (*get)(char *buf, const struct kernel_param *kp); };

static int scnprintf(char *buf, size_t sz, const char *fmt, ...) {
 va_list ap; int n;
 va_start(ap, fmt); n = vsnprintf(buf, sz, fmt, ap); va_end(ap);
 if (n < 0) return 0;
 return (size_t)n >= sz ? (int)(sz - 1) : n;
}

static bool test_bit(int b, const unsigned long *p) { return ((*p) & (1UL << b)) != 0; }
static void set_bit(int b, unsigned long *p) { *p |= (1UL << b); }

struct hfi_capability { u32 capability_type, min, max, step_size; };
struct hfi_profile_level { u32 profile, level; };
struct hfi_raw_fmts { u32 buftype, fmt; };
struct hfi_plat_caps {
 unsigned int num_caps, num_pl, num_fmts;
 struct hfi_capability caps[2];
 struct hfi_profile_level pl[2];
 struct hfi_raw_fmts fmts[2];
};

#include "../probe/video/sq2-venus-system-pm.h"

struct venus_core {
 struct device *dev;
 struct sq2_venus_platform *sq2_platform;
 struct sq2_system_pm sq2_system_pm;
 struct mutex pm_lock, sq2_pm_mutex, lock, sq2_retire_lock;
 struct list_head instances, sq2_retired;
 atomic_t insts_count;
 unsigned long sys_error;
 unsigned int state, error, max_sessions_supported;
 void *priv, *sq2_fw_owner;
 bool sq2_secure_active, sq2_no_tz_active, sq2_dma_stopped;
 bool sq2_boot_attempted, sq2_pm_enabled, sq2_runtime_suspended;
 unsigned long sq2_vb2_bytes, sq2_rt_stops, sq2_rt_starts;
 struct delayed_work work;
 struct hfi_plat_caps avc_caps;
};

#define IS_SQ2_HFI(c) ((c) != NULL)
static const int venus_core_ops = 0;

/* Mock counters & fault injection */
static int m_mark_busy, m_autosuspend, m_domains_put, m_domains_get;
static int m_domains_powered, m_prepare_pc, m_irq_en, m_fw_shut, m_fw_boot;
static int m_quiet, m_restore, m_hfi_deinit, m_hfi_create, m_sys_quiet_flag;
static int fail_domains_put, fail_domains_get, fail_fw_boot, fail_prepare_pc;

static void pm_runtime_mark_last_busy(struct device *d) { (void)d; m_mark_busy++; }
static void pm_runtime_autosuspend(struct device *d) { (void)d; m_autosuspend++; }
static int pm_runtime_force_suspend(struct device *d) { (void)d; return 0; }
static int pm_runtime_force_resume(struct device *d) { (void)d; return 0; }
static void cancel_delayed_work_sync(struct delayed_work *w) { (void)w; }

static int venus_hfi_prepare_system_stop(struct venus_core *c) {
 (void)c; m_prepare_pc++; return fail_prepare_pc;
}
static int sq2_venus_platform_irq(struct sq2_venus_platform *o, bool en) {
 (void)o; m_irq_en = en ? 1 : 0; return 0;
}
static int venus_shutdown(struct venus_core *c) {
 m_fw_shut++;
 c->sq2_fw_owner = NULL;
 c->sq2_secure_active = false;
 c->sq2_no_tz_active = false;
 return 0;
}
static int hfi_core_deinit(struct venus_core *c, bool b) {
 (void)b; m_hfi_deinit++; c->state = CORE_UNINIT; return 0;
}
static void hfi_destroy(struct venus_core *c) { c->priv = NULL; }
static void sq2_venus_retire_stopped(struct venus_core *c) { c->sq2_dma_stopped = true; }
static int sq2_venus_platform_system_quiet(struct sq2_venus_platform *o) {
 (void)o; m_quiet++; m_sys_quiet_flag = 1; return 0;
}
static int sq2_venus_platform_system_restore(struct sq2_venus_platform *o) {
 (void)o; assert(m_domains_powered == 3 && m_sys_quiet_flag == 1);
 m_restore++; m_sys_quiet_flag = 0; return 0;
}
static int sq2_venus_platform_domains_put(struct sq2_venus_platform *o) {
 (void)o; m_domains_put++;
 if (fail_domains_put) return fail_domains_put;
 m_domains_powered = 0; return 0;
}
static int sq2_venus_platform_domains_get(struct sq2_venus_platform *o) {
 (void)o; m_domains_get++;
 if (fail_domains_get) return fail_domains_get;
 m_domains_powered = 3; return 0;
}
static int hfi_create(struct venus_core *c, const void *ops) {
 (void)ops; m_hfi_create++; c->priv = (void *)0x1; return 0;
}
static int venus_boot(struct venus_core *c) {
 m_fw_boot++;
 if (fail_fw_boot) return fail_fw_boot;
 c->sq2_fw_owner = (void *)0x2;
 c->sq2_secure_active = true;
 c->sq2_no_tz_active = true;
 return 0;
}
static int hfi_core_resume(struct venus_core *c, bool f) { (void)c; (void)f; return 0; }
static int hfi_core_init(struct venus_core *c) { c->state = CORE_INIT; return 0; }
static int venus_enumerate_codecs(struct venus_core *c, int t) { (void)c; (void)t; return 0; }
static const struct hfi_plat_caps *venus_caps_by_codec(struct venus_core *c, u32 codec, int t) {
 (void)t;
 return codec == HFI_VIDEO_CODEC_H264 ? &c->avc_caps : NULL;
}

/* Provide dummy hfi_venus.h stub via -I or include guard */
#include "../probe/video/venus-sq2-system-pm.h"

static struct device g_dev;
static struct venus_core g_core;

static void init_running_core(void) {
 memset(&g_core, 0, sizeof(g_core));
 g_dev.drvdata = &g_core;
 g_core.dev = &g_dev;
 g_core.sq2_platform = (struct sq2_venus_platform *)0xdead;
 g_core.pm_lock.level = 1;
 g_core.sq2_pm_mutex.level = 3;
 g_core.lock.level = 4;
 g_core.sq2_retire_lock.level = 5;
 init_list(&g_core.instances);
 init_list(&g_core.sq2_retired);
 g_core.state = CORE_INIT;
 g_core.error = HFI_ERR_NONE;
 g_core.priv = (void *)0x1;
 g_core.sq2_fw_owner = (void *)0x2;
 g_core.sq2_secure_active = true;
 g_core.sq2_no_tz_active = true;
 g_core.sq2_pm_enabled = true;
 g_core.max_sessions_supported = 16;
 g_core.avc_caps.num_caps = 1;
 g_core.avc_caps.caps[0] = (struct hfi_capability){1, 96, 8192, 1};
 g_core.avc_caps.num_pl = 1;
 g_core.avc_caps.pl[0] = (struct hfi_profile_level){32, 131072};
 g_core.avc_caps.num_fmts = 1;
 g_core.avc_caps.fmts[0] = (struct hfi_raw_fmts){2, 32770};
 held_mask = 0; forbid_pm_lock = false;
 m_mark_busy = m_autosuspend = m_domains_put = m_domains_get = 0;
 m_domains_powered = 3; m_prepare_pc = m_fw_shut = m_fw_boot = 0;
 m_quiet = m_restore = m_hfi_deinit = m_hfi_create = m_sys_quiet_flag = 0;
 m_irq_en = 1;
 fail_domains_put = fail_domains_get = fail_fw_boot = fail_prepare_pc = 0;
 sq2_runtime_pm = true;
 sq2_pm_status_core = NULL;
}

int main(void) {
 char buf[PAGE_SIZE];
 struct kernel_param kp = {0};

 /* 1. Publish arms autosuspend on core->dev. */
 init_running_core();
 venus_sq2_pm_publish(&g_core);
 assert(m_mark_busy == 1 && m_autosuspend == 1);

 /* 2. Runtime suspend MUST NOT acquire pm_lock (prevents vdec_pm_get deadlock). */
 forbid_pm_lock = true;
 assert(venus_sq2_runtime_suspend(&g_dev) == 0);
 forbid_pm_lock = false;
 assert(held_mask == 0);
 assert(g_core.sq2_system_pm.phase == SQ2_PM_OFF);
 assert(g_core.sq2_runtime_suspended == true);
 assert(g_core.sq2_rt_stops == 1 && g_core.sq2_rt_starts == 0);
 assert(m_domains_put == 1 && m_domains_powered == 0 && m_sys_quiet_flag == 1);

 /* 3. Cached sq2_avc_caps still readable while SQ2_PM_OFF (zero wakeups!). */
 assert(sq2_avc_caps_get(buf, &kp) > 0);
 assert(strstr(buf, "sessions=16") && strstr(buf, "end=1"));
 assert(m_fw_boot == 0 && g_core.sq2_system_pm.phase == SQ2_PM_OFF);

 /* 4. sq2_pm_status reports legacy + runtime PM fields cleanly. */
 assert(venus_sq2_pm_status_get(buf, &kp) > 0);
 assert(strstr(buf, "phase=1") && strstr(buf, "rtpm=1 rt_sus=1 rt_stops=1 rt_starts=0"));

 /* 5. s2idle while already runtime-suspended is 0ms no-op on suspend AND lazy on resume! */
 assert(venus_sq2_system_suspend(&g_dev) == 0);
 assert(m_fw_shut == 1 && m_domains_put == 1);
 assert(venus_sq2_system_resume(&g_dev) == 0);
 assert(m_fw_boot == 0 && m_domains_get == 0);
 assert(g_core.sq2_system_pm.phase == SQ2_PM_OFF && g_core.sq2_runtime_suspended);

 /* 6. Demand runtime resume wakes GDSCs + restores + boots firmware WITHOUT pm_lock! */
 /* Simulate vdec_pm_get holding pm_lock across synchronous parent runtime resume. */
 mutex_lock(&g_core.pm_lock);
 forbid_pm_lock = true;
 assert(venus_sq2_runtime_resume(&g_dev) == 0);
 forbid_pm_lock = false;
 mutex_unlock(&g_core.pm_lock);
 assert(held_mask == 0);
 assert(g_core.sq2_system_pm.phase == SQ2_PM_RUNNING);
 assert(g_core.sq2_runtime_suspended == false);
 assert(g_core.sq2_rt_starts == 1 && m_domains_get == 1 && m_domains_powered == 3);
 assert(m_fw_boot == 1 && g_core.state == CORE_INIT);

 /* 7. s2idle while ACTIVE uses pure system_quiet/restore (NEVER calls domains_put/get). */
 assert(venus_sq2_system_suspend(&g_dev) == 0);
 assert(g_core.sq2_system_pm.phase == SQ2_PM_OFF && !g_core.sq2_runtime_suspended);
 assert(m_domains_put == 1 && m_domains_powered == 3);
 assert(venus_sq2_system_resume(&g_dev) == 0);
 assert(g_core.sq2_system_pm.phase == SQ2_PM_RUNNING && !g_core.sq2_runtime_suspended);
 assert(m_domains_get == 1 && m_domains_powered == 3);

 /* 8. Active session blocks runtime suspend non-terminally (-EBUSY). */
 g_core.insts_count.v = 1;
 assert(venus_sq2_runtime_suspend(&g_dev) == -EBUSY);
 assert(g_core.sq2_system_pm.phase == SQ2_PM_RUNNING && !test_bit(0, &g_core.sys_error));
 g_core.insts_count.v = 0;

 /* 9. sq2_runtime_pm=false opt-out keeps core resident (-EBUSY). */
 sq2_runtime_pm = false;
 assert(venus_sq2_runtime_suspend(&g_dev) == -EBUSY);
 assert(g_core.sq2_system_pm.phase == SQ2_PM_RUNNING);
 sq2_runtime_pm = true;

 /* 10. TZ-mode boot (sq2_no_tz_active=false) returns -EBUSY (never poisons runtime_error). */
 g_core.sq2_no_tz_active = false;
 assert(venus_sq2_runtime_suspend(&g_dev) == -EBUSY);
 assert(g_core.sq2_system_pm.phase == SQ2_PM_RUNNING && !test_bit(0, &g_core.sys_error));
 g_core.sq2_no_tz_active = true;

 /* 11. Runtime resume firmware failure quarantines SQ2_PM_BROKEN and releases GDSCs! */
 init_running_core();
 venus_sq2_pm_publish(&g_core);
 assert(venus_sq2_runtime_suspend(&g_dev) == 0);
 assert(m_domains_powered == 0);
 fail_fw_boot = -ETIMEDOUT;
 assert(venus_sq2_runtime_resume(&g_dev) == -ETIMEDOUT);
 assert(g_core.sq2_system_pm.phase == SQ2_PM_BROKEN && test_bit(0, &g_core.sys_error));
 assert(m_domains_powered == 0); /* GDSCs released on failed wake! */
 assert(sq2_avc_caps_get(buf, &kp) == -EIO);
 assert(venus_sq2_runtime_resume(&g_dev) == -ETIMEDOUT); /* quarantined, no retry */
 assert(m_fw_boot == 1);

 /* 12. domains_put failure quarantines SQ2_PM_BROKEN cleanly. */
 init_running_core();
 venus_sq2_pm_publish(&g_core);
 fail_domains_put = -EIO;
 assert(venus_sq2_runtime_suspend(&g_dev) == -EIO);
 assert(g_core.sq2_system_pm.phase == SQ2_PM_BROKEN && test_bit(0, &g_core.sys_error));

 puts("SQ2_VENUS_RUNTIME_PM_PASS: lock-order verified (no pm_lock in rtpm), runtime suspend/resume + GDSC toggle, lazy s2idle wake, cached caps while OFF, TZ/busy/opt-out nonterminal EBUSY, failure quarantine + GDSC release");
 return 0;
}
