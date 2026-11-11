/* Actual bus layer and three setters; replace only external providers/locking. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
typedef uint32_t u32;
typedef uint64_t u64;
typedef int64_t s64;
#define U32_MAX UINT32_MAX
#define GFP_KERNEL 0
#define KGSL_MAX_PWRLEVELS 16
#define MAX_GX_LEVELS 16
#define KGSL_PWRFLAGS_AXI_ON 2
#define KGSL_PWRFLAGS_ON 1
#define KGSL_PWRFLAGS_OFF 0
#define INVALID_DCVS_IDX UINT32_MAX
#define EPROBE_DEFER 517
#define MBps_to_icc(v) ((v) * 1000U)
#define kBps_to_icc(v) (v)
#define max(x, y) ((x) > (y) ? (x) : (y))
#define ERR_PTR(n) ((void *)(intptr_t)(n))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define IS_ERR_OR_NULL(p) (!(p) || IS_ERR(p))
#define clamp_t(t, x, lo, hi) ({ t _x=(x), _lo=(lo), _hi=(hi); _x<_lo?_lo:_x>_hi?_hi:_x; })
static u64 div_u64(u64 x, u32 y) { assert(y); return x / y; }
static bool test_bit(unsigned b, const unsigned long *p) { return !!(*p & (1UL << b)); }
static void set_bit(unsigned b, unsigned long *p) { *p |= 1UL << b; }
static void clear_bit(unsigned b, unsigned long *p) { *p &= ~(1UL << b); }
enum kgsl_bus_vote { KGSL_BUS_VOTE_OFF, KGSL_BUS_VOTE_ON, KGSL_BUS_VOTE_MINIMUM };
struct kgsl_pwrlevel { unsigned gpu_freq, bus_freq, bus_min, bus_max; };
struct kgsl_pwrctrl {
    bool icc_dirty;
    struct kgsl_pwrlevel pwrlevels[KGSL_MAX_PWRLEVELS];
    unsigned num_pwrlevels, active_pwrlevel, default_pwrlevel;
    int cur_buslevel, bus_mod, ddr_table_count;
    unsigned long power_flags, ctrl_flags, bus_ab_mbytes;
    u32 cur_ab, cur_ib, bootstrap_ib, bus_percent_ab, bus_width;
    bool bus_control;
    u32 *ddr_table;
    void *icc_path;
};
struct kgsl_device;
struct device_node { bool native; };
struct device { struct kgsl_device *data; struct device_node *of_node; };
struct platform_device { struct device dev; };
struct kgsl_functable { int (*gpu_bus_set)(struct kgsl_device *, int, u32); };
struct kgsl_device {
    struct kgsl_pwrctrl pwrctrl;
    const struct kgsl_functable *ftbl;
    struct device *dev;
    int mutex;
    struct { bool enabled; struct { u32 flag, percent_ab; unsigned long ab_mbytes; } bus_profile; } pwrscale;
};
struct adreno_device { struct kgsl_device device; };
#define KGSL_DEVICE(a) (&(a)->device)
static struct kgsl_device *dev_get_drvdata(struct device *d) { return d->data; }
static void mutex_lock(int *m) { assert(!*m); *m = 1; }
static void mutex_unlock(int *m) { assert(*m); *m = 0; }
static bool _check_fast_hint(u32 f) { return f & 1; }
static bool _check_slow_hint(u32 f) { return f & 2; }
static int icc_error, dcvs_error, icc_calls, dcvs_calls, traces, axi_traces;
static u32 last_ab, last_peak;
static int last_level, backend;
static int icc_set_bw(void *path, u32 ab, u32 peak)
{
    assert(path && !IS_ERR(path)); ++icc_calls;
    last_ab = ab; last_peak = peak; return icc_error;
}
static int a6xx_gmu_dcvs_set(struct adreno_device *dev, u32 freq, int level)
{
    assert(dev && freq == INVALID_DCVS_IDX); ++dcvs_calls; last_level = level; return dcvs_error;
}
static int a6xx_hwsched_dcvs_set(struct adreno_device *dev, u32 freq, int level)
{ return a6xx_gmu_dcvs_set(dev, freq, level); }
static void trace_kgsl_buslevel(struct kgsl_device *dev, unsigned active, int level)
{ assert(active == dev->pwrctrl.active_pwrlevel && level == dev->pwrctrl.cur_buslevel); ++traces; }
static void trace_kgsl_bus(struct kgsl_device *dev, int state)
{ assert(test_bit(KGSL_PWRFLAGS_AXI_ON, &dev->pwrctrl.power_flags) == (state == KGSL_PWRFLAGS_ON)); ++axi_traces; }
static u32 input_table[] = { 0, 100000, 200000, 400000 };
static int ddr_type, typed_error, generic_error, table_calls, allocs, frees, get_calls, put_calls;
static void *path_result;
struct dev_pm_opp {unsigned long freq, peak, avg;};
static struct dev_pm_opp opps[KGSL_MAX_PWRLEVELS];
static int opp_count,opp_refs,opp_gets,opp_puts,opp_error_at,allocation_error,icc_names,icc_index;
static bool of_device_is_compatible(struct device_node *n,const char *name){assert(!strcmp(name,"qcom,adreno-680.1"));return n&&n->native;}
static int of_property_count_strings(struct device_node *n,const char *name){assert(n&&!strcmp(name,"interconnect-names"));return icc_names;}
static int of_property_match_string(struct device_node *n,const char *name,const char *value){assert(n&&!strcmp(name,"interconnect-names")&&!strcmp(value,"gfx-mem"));return icc_index;}
static struct dev_pm_opp *dev_pm_opp_find_freq_exact(struct device *dev,unsigned long freq,bool available){
 assert(dev&&available&&!opp_refs);opp_gets++;if(opp_gets==opp_error_at)return ERR_PTR(-EIO);
 for(int i=0;i<opp_count;i++)if(opps[i].freq==freq){opp_refs++;return &opps[i];}
 return ERR_PTR(-ERANGE);
}
static unsigned long dev_pm_opp_get_bw(struct dev_pm_opp *opp,bool peak,int index){assert(opp_refs==1&&index==0);return peak?opp->peak:opp->avg;}
static void dev_pm_opp_put(struct dev_pm_opp *opp){assert(opp_refs==1);opp_refs--;opp_puts++;}
static void *kcalloc(size_t n,size_t size,int flags){if(allocation_error)return NULL;void *p=calloc(n,size);assert(p);allocs++;return p;}

static int kgsl_get_ddrtype(void) { return ddr_type; }
static u32 *kgsl_bus_get_table(struct platform_device *pdev, const char *name, int *count)
{
    int error;
    assert(pdev); ++table_calls; *count = 0;
    if (!strcmp(name, "qcom,bus-table-ddr7")) error = typed_error;
    else { assert(!strcmp(name, "qcom,bus-table-ddr")); error = generic_error; }
    if (error) return ERR_PTR(error);
    u32 *p = malloc(sizeof(input_table)); assert(p); ++allocs;
    memcpy(p, input_table, sizeof(input_table)); *count = 4; return p;
}
static void kfree(void *p) { assert(!IS_ERR(p)); if (p) { ++frees; free(p); } }
static void *of_icc_get(struct device *dev, const char *name)
{ assert(dev && !strcmp(name, "gfx-mem")); ++get_calls; return path_result; }
static void icc_put(void *p) { assert(!IS_ERR(p)); if (p) { assert(p == path_result); ++put_calls; } }
#include "kgsl-bus-functions.h"
static int set_bus(struct kgsl_device *d, int level, u32 ab)
{
    struct adreno_device *a = (struct adreno_device *)d;
    if (backend == 0) return adreno_interconnect_bus_set(a, level, ab);
    if (backend == 1) return a6xx_gmu_bus_set(a, level, ab);
    return a6xx_hwsched_bus_set(a, level, ab);
}
static const struct kgsl_functable ftbl = { set_bus };
static struct adreno_device fresh(void)
{
    assert(!opp_refs);opp_count=opp_gets=opp_puts=opp_error_at=allocation_error=icc_index=0;icc_names=1;memset(opps,0,sizeof(opps));
    icc_error = dcvs_error = icc_calls = dcvs_calls = traces = axi_traces = 0;
    last_ab = last_peak = 0; last_level = -1;
    ddr_type = -ENOENT; typed_error = generic_error = table_calls = 0;
    allocs = frees = get_calls = put_calls = 0; path_result = input_table;
    input_table[0] = 0; input_table[1] = 100000; input_table[2] = 200000; input_table[3] = 400000;
    return (struct adreno_device){ .device = { .ftbl = &ftbl, .pwrscale.enabled = true, .pwrctrl = {
        .pwrlevels = {{700000000, 3, 1, 3}, {177000000, 1, 1, 2}},
        .num_pwrlevels = 2, .active_pwrlevel = 1, .default_pwrlevel = 1,
        .cur_buslevel = 1, .cur_ab = 10, .cur_ib = backend ? 0 : 100000, .ddr_table = input_table, .ddr_table_count = 4,
        .icc_path = input_table, .bus_control = true, .power_flags = 1UL << KGSL_PWRFLAGS_AXI_ON,
    }}};
}
int main(void)
{
    unsigned cases = 0;
    struct adreno_device a;
    struct kgsl_pwrctrl *p;
    /* Probe rejects all malformed entries, including the last/sparse slot. */
    for (int scenario = 0; scenario < 14; ++scenario) {
        a = fresh(); p = &a.device.pwrctrl; int count = 4;
        if (scenario == 1) count = 0;
        if (scenario == 2) input_table[0] = 1;
        if (scenario == 3) input_table[2] = input_table[1];
        if (scenario == 4) input_table[2] = 1;
        if (scenario == 5) p->num_pwrlevels = 0;
        if (scenario == 6) p->num_pwrlevels = KGSL_MAX_PWRLEVELS + 1;
        if (scenario == 7) p->active_pwrlevel = 2;
        if (scenario == 8) p->default_pwrlevel = 2;
        if (scenario == 9) p->pwrlevels[1].gpu_freq = 0;
        if (scenario == 10) p->pwrlevels[1].bus_min = 0;
        if (scenario == 11) p->pwrlevels[1].bus_freq = 0;
        if (scenario == 12) p->pwrlevels[1].bus_max = 4;
        if (scenario == 13) p->pwrlevels[1].bus_freq = 3;
        struct kgsl_pwrctrl before = *p;
        assert(validate_pwrlevels(&a.device, input_table, count) == (scenario ? -EINVAL : 0));
        assert(!memcmp(p, &before, sizeof(before))); ++cases;
    }
    for (int scenario = 0; scenario < 10; ++scenario) {
        a = fresh(); p = &a.device.pwrctrl; p->ddr_table = NULL; p->icc_path = NULL; p->ddr_table_count = 0;
        struct platform_device pdev = {0}; int expected = 0;
        if (scenario == 1) path_result = ERR_PTR(expected = -EPROBE_DEFER);
        if (scenario == 2) path_result = ERR_PTR(expected = -EIO);
        if (scenario == 3) { path_result = NULL; expected = -ENODEV; }
        if (scenario == 4) generic_error = expected = -ENOENT;
        if (scenario == 5) { p->pwrlevels[1].gpu_freq = 0; expected = -EINVAL; }
        if (scenario >= 6) ddr_type = 7;
        if (scenario == 7) typed_error = -ENOENT;
        if (scenario == 8) typed_error = expected = -ENOMEM;
        if (scenario == 9) typed_error = expected = -EINVAL;
        assert(kgsl_bus_init(&a.device, &pdev) == expected);
        assert(table_calls == (scenario == 7 ? 2 : 1));
        if (expected) assert(!p->ddr_table && !p->ddr_table_count && !p->icc_path && allocs == frees);
        else assert(p->ddr_table && p->ddr_table_count == 4 && p->icc_path == path_result);
        kgsl_bus_close(&a.device); kgsl_bus_close(&a.device);
        assert(allocs == frees && put_calls == !expected); ++cases;
    }
    /* Native OPPs are synthetic inputs, not target bandwidth calibration. */
    for(int scenario=0;scenario<19;scenario++) {
        a=fresh();p=&a.device.pwrctrl;p->ddr_table=NULL;p->ddr_table_count=0;p->icc_path=NULL;
        struct device_node node={.native=true};struct platform_device pdev={.dev.of_node=&node};int expected=0;
        p->num_pwrlevels=4;p->active_pwrlevel=p->default_pwrlevel=1;opp_count=4;
        for(int i=0;i<4;i++){opps[i]=(struct dev_pm_opp){.freq=100000000UL*(i+1),.peak=100000UL*(i/2+1)};p->pwrlevels[3-i].gpu_freq=opps[i].freq;}
        if(scenario==1){opps[0].peak=0;expected=-ENODATA;}
        if(scenario==2){opps[3].peak=0;expected=-ENODATA;}
        if(scenario==3){opps[1].peak=1;expected=-EINVAL;}
        if(scenario==4){opps[3].peak=1UL+U32_MAX;expected=-ERANGE;}
        if(scenario==5){opps[1].avg=1000;expected=-EOPNOTSUPP;}
        if(scenario==6){opp_error_at=3;expected=-EIO;}
        if(scenario==7){opp_count=2;expected=-ERANGE;}
        if(scenario==8){icc_names=2;expected=-EINVAL;}
        if(scenario==9){icc_index=1;expected=-EINVAL;}
        if(scenario==10){p->pwrlevels[2].gpu_freq=p->pwrlevels[3].gpu_freq;expected=-EINVAL;}
        if(scenario==11){allocation_error=1;expected=-ENOMEM;}
        if(scenario==12){path_result=ERR_PTR(-EPROBE_DEFER);expected=-EPROBE_DEFER;}
        if(scenario==13){path_result=NULL;expected=-ENODEV;}
        if(scenario==14){p->num_pwrlevels=0;expected=-EINVAL;}
        if(scenario==15){p->num_pwrlevels=KGSL_MAX_PWRLEVELS+1;expected=-EINVAL;}
        if(scenario==16){p->active_pwrlevel=4;expected=-EINVAL;}
        if(scenario==17){p->default_pwrlevel=4;expected=-EINVAL;}
        if(scenario==18){p->num_pwrlevels=opp_count=KGSL_MAX_PWRLEVELS;for(int i=0;i<KGSL_MAX_PWRLEVELS;i++){opps[i]=(struct dev_pm_opp){.freq=10000000UL*(i+1),.peak=10000UL*(i+1)};p->pwrlevels[KGSL_MAX_PWRLEVELS-1-i].gpu_freq=opps[i].freq;}expected=-E2BIG;}
        struct kgsl_pwrlevel saved[KGSL_MAX_PWRLEVELS];memcpy(saved,p->pwrlevels,sizeof(saved));
        assert(kgsl_bus_init(&a.device,&pdev)==expected&&!opp_refs&&!table_calls);
        if(expected){assert(!memcmp(saved,p->pwrlevels,sizeof(saved))&&!p->ddr_table&&!p->ddr_table_count&&!p->icc_path&&allocs==frees);}
        else {
            assert(p->ddr_table_count==3&&p->ddr_table[0]==0&&p->ddr_table[1]==100000&&p->ddr_table[2]==200000&&opp_gets==4&&opp_puts==4);
            for(int i=0;i<4;i++)assert(p->pwrlevels[i].bus_freq==(unsigned)(2-i/2)&&p->pwrlevels[i].bus_min==1&&p->pwrlevels[i].bus_max==p->pwrlevels[i].bus_freq);
            assert(!validate_pwrlevels(&a.device,p->ddr_table,p->ddr_table_count));
        }
        kgsl_bus_close(&a.device);assert(allocs==frees);++cases;
    }
    /* Wide arithmetic and MB/s -> ICC range boundaries. */
    a = fresh(); p = &a.device.pwrctrl; u32 ab = 123;
    assert(!_ab_buslevel_update(p, UINT32_MAX, false, &ab) && ab == 1073741); ++cases;
    p->bus_percent_ab = 100; input_table[3] = UINT32_MAX;
    assert(!_ab_buslevel_update(p, UINT32_MAX, false, &ab) && ab == 4294967); ++cases;
    p->bus_percent_ab = 101;
    assert(_ab_buslevel_update(p, 1, false, &ab) == -EINVAL); ++cases;
    p->bus_width = 16; p->bus_ab_mbytes = UINT32_MAX / 1000;
    assert(!_ab_buslevel_update(p, 1, false, &ab) && ab == UINT32_MAX / 1000); ++cases;
    p->bus_ab_mbytes++;
    assert(_ab_buslevel_update(p, 1, false, &ab) == -ERANGE); ++cases;
    p->bus_ab_mbytes = ULONG_MAX;
    assert(_ab_buslevel_update(p, 1, false, &ab) == -ERANGE); ++cases;
    assert(!_ab_buslevel_update(p, 100000, true, &ab) && ab == 25); ++cases;
    assert(!_ab_buslevel_update(p, 0, false, &ab) && ab == 0); ++cases;
    for (backend = 0; backend < 3; ++backend) {
        /* Failure retains request; GMU success is not rolled back in software. */
        for (int vote = KGSL_BUS_VOTE_OFF; vote <= KGSL_BUS_VOTE_MINIMUM; ++vote) {
            a = fresh(); p = &a.device.pwrctrl;
            p->bus_mod = 1; p->bus_percent_ab = 30; p->bus_ab_mbytes = 10; p->bus_width = 16;
            if (vote == KGSL_BUS_VOTE_MINIMUM) p->cur_buslevel = 2;
            icc_error = -EIO;
            /* ON needs a changed AB to exercise ICC. */
            if (vote == KGSL_BUS_VOTE_ON) p->bus_ab_mbytes = 20;
            unsigned long saved_ab = p->bus_ab_mbytes;
            assert(kgsl_bus_update(&a.device, vote) == -EIO);
            assert(p->bus_mod == 1 && p->bus_percent_ab == 30 && p->bus_ab_mbytes == saved_ab);
            assert(p->cur_ab == 10); int dcvs = dcvs_calls;
            icc_error = 0;
            assert(!kgsl_bus_update(&a.device, vote));
            assert(dcvs_calls == dcvs && icc_calls == 2);
            if (vote != KGSL_BUS_VOTE_ON) assert(!p->bus_mod && !p->bus_percent_ab && !p->bus_ab_mbytes);
            else assert(p->bus_mod == 1 && p->bus_ab_mbytes == 20);
            ++cases;
        }
        for (int on = 0; on < 2; ++on) {
            a = fresh(); p = &a.device.pwrctrl;
            if (on) { clear_bit(KGSL_PWRFLAGS_AXI_ON, &p->power_flags); p->cur_buslevel = 0; p->cur_ab = 0; p->cur_ib = 0; }
            icc_error = -EIO;
            assert(kgsl_pwrctrl_axi(&a.device, on) == -EIO);
            assert(test_bit(KGSL_PWRFLAGS_AXI_ON, &p->power_flags) == !on && !axi_traces);
            icc_error = 0; assert(!kgsl_pwrctrl_axi(&a.device, on));
            assert(test_bit(KGSL_PWRFLAGS_AXI_ON, &p->power_flags) == !!on && axi_traces == 1);
            assert(icc_calls == 2); ++cases;
        }
        /* Cancel a partially completed ON even though the AXI flag was restored OFF. */
        a = fresh(); p = &a.device.pwrctrl;
        clear_bit(KGSL_PWRFLAGS_AXI_ON, &p->power_flags); p->cur_buslevel = 0; p->cur_ab = 0; p->cur_ib = 0;
        icc_error = -EIO;
        assert(kgsl_pwrctrl_axi(&a.device, KGSL_PWRFLAGS_ON) == -EIO);
        assert(!test_bit(KGSL_PWRFLAGS_AXI_ON, &p->power_flags));
        assert(p->cur_buslevel == (backend ? 1 : 0));
        icc_error = 0;
        assert(!kgsl_pwrctrl_axi(&a.device, KGSL_PWRFLAGS_OFF));
        assert(!p->cur_buslevel && !p->cur_ab && !test_bit(KGSL_PWRFLAGS_AXI_ON, &p->power_flags)); ++cases;
        /* Retry through actual devfreq callback; percent-only changes matter. */
        a = fresh(); p = &a.device.pwrctrl; p->bus_width = 16;
        struct device dev = { .data=&a.device }; unsigned long freq = 177000000;
        a.device.pwrscale.bus_profile.flag = 1; a.device.pwrscale.bus_profile.ab_mbytes = 20;
        a.device.pwrscale.bus_profile.percent_ab = 40; icc_error = -EIO;
        assert(kgsl_busmon_target(&dev, &freq, 0) == -EIO);
        assert(!p->bus_mod && !p->bus_percent_ab && !p->bus_ab_mbytes && !a.device.mutex);
        a.device.pwrscale.bus_profile.flag = 1; icc_error = 0;
        assert(!kgsl_busmon_target(&dev, &freq, 0));
        assert(p->bus_mod == 1 && p->cur_buslevel == 2 && p->cur_ab == 20 && icc_calls == 2); ++cases;
        p->bus_width = 0; a.device.pwrscale.bus_profile.percent_ab = 50;
        assert(!kgsl_busmon_target(&dev, &freq, 0)); assert(p->cur_ab == 200); ++cases;
        for (int bad = 0; bad < 5; ++bad) {
            a = fresh(); p = &a.device.pwrctrl;
            if (bad == 0) p->active_pwrlevel = UINT32_MAX;
            if (bad == 1) p->num_pwrlevels = KGSL_MAX_PWRLEVELS + 1;
            if (bad == 2) p->ddr_table = NULL;
            if (bad == 3) p->pwrlevels[0].bus_max = 4;
            if (bad == 4) p->ddr_table_count = 1;
            assert(kgsl_bus_update(&a.device, KGSL_BUS_VOTE_ON) == -EINVAL);
            assert(!icc_calls && !dcvs_calls); ++cases;
        }
        a = fresh(); p = &a.device.pwrctrl; p->bus_mod = INT_MAX;
        assert(!kgsl_bus_update(&a.device, KGSL_BUS_VOTE_ON) && p->cur_buslevel == 3); ++cases;
        p->bus_mod = INT_MIN;
        assert(!kgsl_bus_update(&a.device, KGSL_BUS_VOTE_ON) && p->cur_buslevel == 1); ++cases;
        assert(kgsl_bus_update(&a.device, 99) == -EINVAL); ++cases;
    }
    printf("PASS: %u bus validation/unit/failure/partial-vote/retry scenarios\n", cases);
    return 0;
}
