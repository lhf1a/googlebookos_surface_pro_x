/* Actual donor bus setters, with only hardware calls replaced for fault injection. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
typedef uint32_t u32;
struct kgsl_pwrctrl {
    bool icc_dirty;
    int cur_buslevel, active_pwrlevel;
    u32 cur_ab, cur_ib, bootstrap_ib;
    u32 *ddr_table;
    void *icc_path;
};
struct kgsl_device { struct kgsl_pwrctrl pwrctrl; };
struct adreno_device { struct kgsl_device device; };
#define KGSL_DEVICE(a) (&(a)->device)
#define INVALID_DCVS_IDX UINT32_MAX
#define MBps_to_icc(v) ((v) * 1000U)
#define kBps_to_icc(v) (v)
#define U32_MAX UINT32_MAX
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR_OR_NULL(p) (!(p) || (uintptr_t)(p) >= (uintptr_t)-4095)
#define max(x, y) ((x) > (y) ? (x) : (y))
static int icc_error, dcvs_error, icc_calls, dcvs_calls, traces;
static u32 last_ab, last_peak;
static int last_level;
static int icc_set_bw(void *path, u32 ab, u32 peak)
{
    assert(path);
    ++icc_calls;
    last_ab = ab;
    last_peak = peak;
    return icc_error;
}
static int a6xx_gmu_dcvs_set(struct adreno_device *dev, u32 freq, int level)
{
    assert(dev && freq == INVALID_DCVS_IDX);
    ++dcvs_calls;
    last_level = level;
    return dcvs_error;
}
static int a6xx_hwsched_dcvs_set(struct adreno_device *dev, u32 freq, int level)
{
    return a6xx_gmu_dcvs_set(dev, freq, level);
}
static void trace_kgsl_buslevel(struct kgsl_device *dev, int active, int level)
{
    assert(dev && active == dev->pwrctrl.active_pwrlevel);
    assert(level == dev->pwrctrl.cur_buslevel);
    ++traces;
}
#include "kgsl-icc-functions.h"
static u32 ddr_table[] = { 0, 100000, 200000 };
static struct adreno_device fresh(unsigned backend)
{
    icc_error = dcvs_error = icc_calls = dcvs_calls = traces = 0;
    last_ab = last_peak = 0;
    last_level = -1;
    return (struct adreno_device){ .device.pwrctrl = {
        .cur_buslevel = 1, .cur_ab = 10, .cur_ib = backend ? 0 : 100000, .ddr_table = ddr_table,
        .icc_path = ddr_table,
    }};
}
int main(void)
{
    int cases = 0;
    const int errors[] = { -EIO, -ENOMEM, -EINVAL };
    int (*setters[])(struct adreno_device *, int, u32) = {
        adreno_interconnect_bus_set, a6xx_gmu_bus_set, a6xx_hwsched_bus_set,
    };
    for (unsigned int fn = 0; fn < 3; ++fn) {
        bool gmu = fn != 0;
        struct adreno_device dev = fresh(fn);
        assert(setters[fn](&dev, 1, 10) == 0);
        assert(icc_calls == 0 && dcvs_calls == 0 && traces == 0);
        ++cases;
        for (unsigned int e = 0; e < 3; ++e) {
            dev = fresh(fn);
            icc_error = errors[e];
            assert(setters[fn](&dev, 2, 20) == errors[e]);
            assert(icc_calls == 1 && dcvs_calls == (gmu ? 1 : 0));
            assert(dev.device.pwrctrl.cur_ab == 10);
            /* A completed GMU vote stays committed if the subsequent ICC vote fails. */
            assert(dev.device.pwrctrl.cur_buslevel == (gmu ? 2 : 1));
            assert(traces == (gmu ? 1 : 0));
            assert(last_ab == 20000 && last_peak == (gmu ? 0 : 200000));
            if (gmu) assert(last_level == 2);
            ++cases;
            icc_error = 0;
            assert(setters[fn](&dev, 2, 20) == 0);
            assert(dev.device.pwrctrl.cur_ab == 20 && dev.device.pwrctrl.cur_buslevel == 2);
            assert(icc_calls == 2 && dcvs_calls == (gmu ? 1 : 0) && traces == 1);
            ++cases;
            if (gmu) {
                dev = fresh(fn);
                dcvs_error = errors[e];
                assert(setters[fn](&dev, 2, 20) == errors[e]);
                assert(dcvs_calls == 1 && icc_calls == 0 && traces == 0);
                assert(dev.device.pwrctrl.cur_buslevel == 1 && dev.device.pwrctrl.cur_ab == 10);
                ++cases;
            }
        }
        dev = fresh(fn);
        icc_error = -EIO;
        assert(setters[fn](&dev, 1, 20) == -EIO);
        assert(dev.device.pwrctrl.cur_ab == 10 && dcvs_calls == 0 && traces == 0);
        ++cases;
        icc_error = 0;
        assert(setters[fn](&dev, 1, 20) == 0);
        assert(dev.device.pwrctrl.cur_ab == 20 && icc_calls == 2);
        ++cases;
        dev = fresh(fn);
        assert(setters[fn](&dev, 2, 10) == 0);
        assert(dev.device.pwrctrl.cur_buslevel == 2 && dev.device.pwrctrl.cur_ab == 10);
        assert(icc_calls == (gmu ? 0 : 1) && dcvs_calls == (gmu ? 1 : 0));
        ++cases;
        assert(setters[fn](&dev, 0, 0) == 0);
        assert(dev.device.pwrctrl.cur_buslevel == 0 && dev.device.pwrctrl.cur_ab == 0);
        assert(last_ab == 0 && last_peak == 0);
        ++cases;
    }
    printf("KGSL ICC: %d failure/retry/state/unit scenarios passed\n", cases);
    return 0;
}
