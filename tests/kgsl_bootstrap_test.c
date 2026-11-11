/* First-boot control flow is real; firmware, reset, clock and ICC providers are injected. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
typedef uint32_t u32;
#define U32_MAX UINT32_MAX
#define KGSL_MAX_PWRLEVELS 16
#define KGSL_STATE_NONE 0
#define KGSL_STATE_AWARE 1
#define KGSL_PWRFLAGS_OFF 0
#define GPU_HW_ACTIVE 0
#define GMU_PRIV_PDC_RSC_LOADED 1
#define ADRENO_QUIRK_HFI_USE_REG 1
#define MBps_to_icc(v) ((v) * 1000U)
#define kBps_to_icc(v) (v)
#define max(x, y) ((x) > (y) ? (x) : (y))
#define ERR_PTR(n) ((void *)(intptr_t)(n))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR_OR_NULL(p) (!(p) || (uintptr_t)(p) >= (uintptr_t)-4095)
struct kgsl_pwrlevel { u32 bus_min; };
struct kgsl_pwrctrl {
    bool icc_dirty;
    u32 cur_ab, cur_ib, bootstrap_ib;
    u32 num_pwrlevels, default_pwrlevel;
    struct kgsl_pwrlevel pwrlevels[KGSL_MAX_PWRLEVELS];
    u32 *ddr_table;
    int ddr_table_count;
    void *icc_path;
};
struct kgsl_device { struct kgsl_pwrctrl pwrctrl; void *dev; int state; bool gmu_fault; };
struct a6xx_gmu_device { unsigned long flags; int idle_level, num_clks, power_error; void *clks, *cx_gdsc; };
struct adreno_device { struct kgsl_device device; struct a6xx_gmu_device gmu; bool acd_enabled, quirk; };
#define KGSL_DEVICE(a) (&(a)->device)
#define to_a6xx_gmu(a) (&(a)->gmu)
#define ADRENO_QUIRK(a, q) ((a)->quirk)
static bool test_bit(unsigned b, const unsigned long *p) { return !!(*p & (1UL << b)); }
static void set_bit(unsigned b, unsigned long *p) { *p |= 1UL << b; }
enum stage { NOFAIL, QMP, GDSC, CLOCKS, LOAD_FW, ITCM, CM3_START, GFX_RAIL, SPTRAC, PDC, GMU_HFI, HFI, HFI_AB };
static enum stage fail_stage;
static bool gdsc, clks, irq, running, hfi_ready, persistent_drop_error, clear_ab_error;
static unsigned icc_calls, stop_calls, suspend_calls, traces, errors, power_enables, start_calls;
static unsigned drop_failures, failed_drops;
static u32 applied_ab, applied_peak;
static int primary_error, icc_start_error, forced_icc_error, reset_error, cx_error;
static int step(enum stage s) { return fail_stage == s ? primary_error : 0; }
static void dev_err(void *dev, const char *fmt, int error) { assert(error < 0); ++errors; }
static int kgsl_bus_set_icc(struct kgsl_device *device, u32 ab, u32 ib);
static int icc_set_bw(void *path, u32 ab, u32 peak)
{
    assert(path && !IS_ERR_OR_NULL(path)); ++icc_calls;
    /* HFI shutdown must retain the floor until CM3 reset/power cleanup. */
    assert(!running || hfi_ready || peak != 0);
    if (forced_icc_error) { applied_ab = ab; applied_peak = peak; return forced_icc_error; }
    if (icc_start_error && !applied_peak && peak) return icc_start_error;
    if (fail_stage == HFI_AB && ab) return primary_error;
    if (clear_ab_error && !ab && applied_ab) { clear_ab_error = false; return -EIO; }
    if (!peak && applied_peak && (persistent_drop_error || drop_failures)) {
        if (drop_failures) --drop_failures;
        ++failed_drops; return -EIO;
    }
    applied_ab = ab; applied_peak = peak; return 0;
}
static void trace_kgsl_pwr_request_state(struct kgsl_device *dev, int state) { assert(state == KGSL_STATE_AWARE); }
static void trace_kgsl_pwr_set_state(struct kgsl_device *dev, int state)
{ assert(state == KGSL_STATE_AWARE && running && hfi_ready && !dev->pwrctrl.bootstrap_ib); ++traces; }
static int a6xx_gmu_aop_send_acd_state(struct a6xx_gmu_device *gmu, bool on) { return step(QMP); }
static int a6xx_gmu_enable_gdsc(struct adreno_device *a)
{
    assert(!gdsc && applied_peak == 300000); ++power_enables;
    int ret = step(GDSC); if (!ret) gdsc = true; return ret;
}
static int a6xx_gmu_enable_clks(struct adreno_device *a)
{
    assert(gdsc && !clks); int ret = step(CLOCKS);
    if (!ret) { clks = true; a->device.state = KGSL_STATE_AWARE; } return ret;
}
static int a6xx_gmu_load_fw(struct adreno_device *a) { assert(clks); return step(LOAD_FW); }
static int a6xx_gmu_itcm_shadow(struct adreno_device *a) { assert(clks); return step(ITCM); }
static void a6xx_gmu_register_config(struct adreno_device *a) { assert(clks); }
static void a6xx_gmu_version_info(struct adreno_device *a) { assert(clks); }
static void a6xx_gmu_irq_enable(struct adreno_device *a) { assert(clks && !irq); irq = true; }
static int a6xx_gmu_device_start(struct adreno_device *a)
{ assert(clks && irq && applied_peak == 300000); running = true; ++start_calls; return step(CM3_START); }
static int a6xx_gmu_gfx_rail_on(struct adreno_device *a) { assert(running); return step(GFX_RAIL); }
static int a6xx_gmu_sptprac_enable(struct adreno_device *a) { assert(running); return step(SPTRAC); }
static int a6xx_load_pdc_ucode(struct adreno_device *a) { assert(clks); return step(PDC); }
static void a6xx_load_rsc_ucode(struct adreno_device *a) { assert(clks); }
static int a6xx_gmu_hfi_start(struct adreno_device *a) { assert(running); return step(GMU_HFI); }
static int a6xx_hfi_start(struct adreno_device *a)
{
    assert(running); int ret = step(HFI); if (ret) return ret;
    ret = kgsl_bus_set_icc(&a->device, 20, 0);
    if (!ret) { assert(applied_peak == 300000); hfi_ready = true; }
    return ret;
}
static int a6xx_hwsched_hfi_start(struct adreno_device *a)
{ int ret = a6xx_gmu_hfi_start(a); return ret ? ret : a6xx_hfi_start(a); }
static int kgsl_pwrctrl_axi(struct kgsl_device *d, int state)
{ assert(state == KGSL_PWRFLAGS_OFF); return kgsl_bus_set_icc(d, 0, 0); }
static void a6xx_hfi_stop(struct adreno_device *a)
{ assert(running && clks && irq); ++stop_calls; hfi_ready = false; kgsl_pwrctrl_axi(&a->device, KGSL_PWRFLAGS_OFF); }
static void a6xx_hwsched_hfi_stop(struct adreno_device *a) { a6xx_hfi_stop(a); }
static int a6xx_gmu_suspend(struct adreno_device *a)
{
    assert(running && clks && gdsc && stop_calls && applied_peak == 300000);
    ++suspend_calls; if (reset_error) { a->gmu.power_error = reset_error; return reset_error; } irq = running = clks = gdsc = false; a->device.state = KGSL_STATE_NONE; return 0;
}
static void clk_bulk_disable_unprepare(int num, void *handles)
{ assert(clks && !running); clks = false; }
static int a6xx_cx_regulator_disable_wait(void *reg, struct kgsl_device *d, u32 timeout)
{ assert(gdsc && !clks && !running && timeout == 5000); if (cx_error) return cx_error; gdsc = false; return 0; }
static int a6xx_gmu_disable_clks(struct adreno_device *a)
{ clk_bulk_disable_unprepare(a->gmu.num_clks, a->gmu.clks); return 0; }
static int a6xx_gmu_disable_cx(struct adreno_device *a)
{ return a6xx_cx_regulator_disable_wait(a->gmu.cx_gdsc, &a->device, 5000); }
static void a6xx_rdpm_cx_freq_update(struct a6xx_gmu_device *gmu, u32 value)
{ assert(!value && !gdsc && !clks); }
#define oob_boot_slumber 7
static void a6xx_gmu_oob_clear(struct kgsl_device *d, int oob) { assert(oob == 7 && running); }
#include "kgsl-bootstrap-functions.h"
static u32 table[] = {0, 300000, 900000};
static struct adreno_device fresh(void)
{
    fail_stage = NOFAIL; primary_error = -ENOMEM; icc_start_error = forced_icc_error = reset_error = cx_error = 0;
    gdsc = clks = irq = running = hfi_ready = persistent_drop_error = clear_ab_error = false;
    icc_calls = stop_calls = suspend_calls = traces = errors = power_enables = start_calls = 0;
    drop_failures = failed_drops = applied_ab = applied_peak = 0;
    return (struct adreno_device){ .quirk = true, .device.pwrctrl = {
        .num_pwrlevels = 1, .pwrlevels = {{1}}, .ddr_table = table, .ddr_table_count = 3, .icc_path = table,
    }};
}
int main(void)
{
    unsigned cases = 0;
    int (*boots[])(struct adreno_device *) = {a6xx_gmu_first_boot, a6xx_hwsched_gmu_first_boot};
    for (unsigned fn = 0; fn < 2; ++fn) {
        for (int st = NOFAIL; st <= HFI_AB; ++st) {
            if (fn && (st == GFX_RAIL || st == SPTRAC)) continue;
            struct adreno_device a = fresh(); fail_stage = st;
            int ret = boots[fn](&a);
            if (!st) {
                assert(!ret && traces == 1 && !stop_calls && !suspend_calls && running && hfi_ready);
                assert(applied_ab == 20000 && !applied_peak && a.device.pwrctrl.cur_ab == 20 && !a.device.pwrctrl.cur_ib);
            } else {
                assert(ret == primary_error && !traces && !running && !gdsc && !clks && !irq);
                assert(!applied_ab && !applied_peak && !a.device.pwrctrl.bootstrap_ib && !a.device.pwrctrl.cur_ab && !a.device.pwrctrl.cur_ib);
                assert(a.device.state == KGSL_STATE_NONE && stop_calls == start_calls && suspend_calls == start_calls);
            }
            ++cases;
        }
        for (int variant = 0; variant < 5; ++variant) {
            struct adreno_device a = fresh();
            if (variant == 0) icc_start_error = -EAGAIN;
            if (variant == 1) drop_failures = 1;
            if (variant == 2) persistent_drop_error = true;
            if (variant == 3) { drop_failures = 1; clear_ab_error = true; }
            if (variant == 4) { fail_stage = LOAD_FW; persistent_drop_error = true; }
            int expected = variant == 0 ? -EAGAIN : variant == 4 ? primary_error : -EIO;
            assert(boots[fn](&a) == expected);
            assert(!running && !gdsc && !clks && !irq && !traces && a.device.state == KGSL_STATE_NONE);
            if (variant == 0) assert(!power_enables && !start_calls);
            else if (variant == 4) assert(!start_calls && !stop_calls && !suspend_calls);
            else assert(stop_calls == 1 && suspend_calls == 1);
            if (variant == 2 || variant == 4) {
                assert(errors && applied_peak == 300000 && a.device.pwrctrl.bootstrap_ib == 300000 && a.device.pwrctrl.cur_ib == 300000);
                persistent_drop_error = false; assert(!kgsl_bus_vote_bootstrap(&a.device, false));
            }
            assert(!applied_ab && !applied_peak && !a.device.pwrctrl.bootstrap_ib && !a.device.pwrctrl.cur_ab && !a.device.pwrctrl.cur_ib);
            ++cases;
        }
        for (int which = 0; which < 2; ++which) {
            struct adreno_device a = fresh();
            if (which) { fail_stage = CM3_START; reset_error = -EIO; }
            else { fail_stage = LOAD_FW; cx_error = -EIO; }
            assert(boots[fn](&a) == primary_error);
            assert(a.gmu.power_error == -EIO && !traces && gdsc);
            assert(applied_peak == 300000 && a.device.pwrctrl.bootstrap_ib == 300000);
            assert(a.device.state == KGSL_STATE_AWARE && running == !!which);
            ++cases;
        }
        /* Already-loaded PDC and non-legacy/non-SPTRAC path. */
        struct adreno_device a = fresh(); a.quirk = false; a.gmu.idle_level = 1;
        set_bit(GMU_PRIV_PDC_RSC_LOADED, &a.gmu.flags); fail_stage = PDC;
        assert(!boots[fn](&a) && applied_ab == 20000 && !applied_peak); ++cases;
    }
    for (int bad = 0; bad < 8; ++bad) {
        struct adreno_device a = fresh(); struct kgsl_pwrctrl *p = &a.device.pwrctrl;
        if (bad == 0) p->num_pwrlevels = 0;
        if (bad == 1) p->num_pwrlevels = KGSL_MAX_PWRLEVELS + 1;
        if (bad == 2) p->default_pwrlevel = 1;
        if (bad == 3) p->ddr_table = NULL;
        if (bad == 4) p->ddr_table_count = 1;
        if (bad == 5) p->pwrlevels[0].bus_min = 0;
        if (bad == 6) p->pwrlevels[0].bus_min = 3;
        if (bad == 7) p->pwrlevels[0].bus_min = UINT32_MAX;
        assert(kgsl_bus_vote_bootstrap(&a.device, true) == -EINVAL && !icc_calls && !p->bootstrap_ib); ++cases;
    }
    struct adreno_device a = fresh();
    assert(kgsl_bus_set_icc(&a.device, UINT32_MAX, 0) == -ERANGE && !icc_calls); ++cases;
    assert(!kgsl_bus_set_icc(&a.device, 20, 100000));
    forced_icc_error = -EIO;
    assert(kgsl_bus_set_icc(&a.device, 40, 200000) == -EIO);
    assert(a.device.pwrctrl.icc_dirty && a.device.pwrctrl.cur_ab == 20 && a.device.pwrctrl.cur_ib == 100000);
    assert(applied_ab == 40000 && applied_peak == 200000); ++cases;
    forced_icc_error = 0;
    assert(!kgsl_bus_set_icc(&a.device, 20, 100000));
    assert(!a.device.pwrctrl.icc_dirty && applied_ab == 20000 && applied_peak == 100000); ++cases;
    a.device.pwrctrl.icc_path = NULL;
    assert(kgsl_bus_set_icc(&a.device, 0, 0) == -ENODEV); ++cases;
    a.device.pwrctrl.icc_path = ERR_PTR(-EAGAIN);
    assert(kgsl_bus_set_icc(&a.device, 0, 0) == -EAGAIN); ++cases;
    printf("PASS: %u first-boot/floor/AB-preservation/unwind/retry scenarios\n", cases);
    return 0;
}
