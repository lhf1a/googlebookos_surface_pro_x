/* Actual init body; host models only its kernel/provider dependencies. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
typedef uint32_t u32;
#include "clock-types.h"
struct device_node { bool native, no_nap, supplies, bimc; };
struct device { struct device_node *of_node; };
struct platform_device { struct device dev; };
struct clk { int id; };
struct regulator { int id; };
struct kgsl_pwrctrl {
    struct clk *grp_clks[KGSL_MAX_CLKS], *gpu_bimc_int_clk;
    u32 gpu_bimc_int_clk_freq, gx_gdsc_parent_min_corner;
    struct regulator *cx_gdsc, *gx_gdsc, *gx_gdsc_parent;
    unsigned int num_pwrlevels, active_pwrlevel, default_pwrlevel;
    unsigned int max_pwrlevel, min_pwrlevel, thermal_pwrlevel;
    unsigned int sysfs_thermal_pwrlevel, cooling_thermal_pwrlevel;
    unsigned int thermal_pwrlevel_floor, wakeup_maxpwrlevel;
    unsigned long ctrl_flags, power_flags;
    struct kgsl_pwrlevel pwrlevels[KGSL_MAX_PWRLEVELS];
    int minbw_timer;
};
struct kgsl_device {
    struct device *dev;
    struct platform_device *pdev;
    struct kgsl_pwrctrl pwrctrl;
    struct { bool components_bound; void *dev_ops; } gmu_core;
    int active_cnt_wq;
};
static struct device_node node;
static struct platform_device platform;
static struct kgsl_device gpu;
static struct clk source_clk={1}, timer_clk={6}, bimc_clk={7};
static struct regulator regulator;
static int get_calls, round_calls, set_calls, isense_calls, reg_calls, optional_clk_calls;
static int pm_calls, timer_calls, wait_calls, get_error, reg_error, property_error;
static int common_clk, scenarios;
static unsigned long round_delta, last_set;
#define BIT(n) (1UL << (n))
#define CONFIG_COMMON_CLK_QCOM common_clk
#define IS_ENABLED(x) (x)
#define PTR_ERR(p) ((long)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
static void log_msg(struct device *d, const char *fmt, ...) __attribute__((format(printf,2,3)));
static void log_msg(struct device *d, const char *fmt, ...) { }
#define dev_err log_msg
#define dev_warn log_msg
static bool of_device_is_compatible(struct device_node *n, const char *s)
{ assert(!strcmp(s,"qcom,adreno-680.1")); return n->native; }
static bool of_property_read_bool(struct device_node *n, const char *s)
{
    if (!strcmp(s,"qcom,no-nap")) return n->no_nap;
    assert(!strcmp(s,"vddcx-supply") || !strcmp(s,"vdd-supply") || !strcmp(s,"vdd-parent-supply"));
    return n->supplies;
}
static int of_property_read_u32(struct device_node *n, const char *s, u32 *v)
{
    if (!strcmp(s,"qcom,gpu-bimc-interface-clk-freq")) {
        if (!n->bimc) return -EINVAL;
        *v=123; return 0;
    }
    assert(!strcmp(s,"vdd-parent-min-corner"));
    if (property_error) return property_error;
    *v=42; return 0;
}
static int _get_clocks(struct kgsl_device *d)
{
    get_calls++;
    if (get_error) return get_error;
    d->pwrctrl.grp_clks[1]=&source_clk;
    d->pwrctrl.grp_clks[6]=&timer_clk;
    return 0;
}
static struct clk *devm_clk_get(struct device *d, const char *s)
{ assert(!strcmp(s,"bimc_gpu_clk")); optional_clk_calls++; return &bimc_clk; }
static long clk_round_rate(struct clk *c, unsigned long rate)
{ assert(c==&source_clk || c==&timer_clk); round_calls++; return rate+round_delta; }
static int clk_set_rate(struct clk *c, unsigned long rate)
{ assert(c==&source_clk || c==&timer_clk); set_calls++; last_set=rate; return 0; }
static int kgsl_pwrctrl_clk_set_rate(struct clk *c, unsigned long rate, const char *name)
{ assert(c==&timer_clk); return clk_set_rate(c,rate); }
static void _isense_clk_set_rate(struct kgsl_pwrctrl *p, unsigned int level)
{ assert(level==p->num_pwrlevels-1); isense_calls++; }
static struct regulator *devm_regulator_get(struct device *d, const char *s)
{
    reg_calls++;
    if (!strcmp(s,"vdd-parent") && reg_error) return (void *)(intptr_t)reg_error;
    return &regulator;
}
static void init_waitqueue_head(int *q) { *q=1; wait_calls++; }
static void pm_runtime_enable(struct device *d) { pm_calls++; }
static void kgsl_minbw_timer(int *t) { }
static void timer_setup(int *t, void (*fn)(int *), int flags)
{ assert(fn==kgsl_minbw_timer && !flags); *t=1; timer_calls++; }
static const char *clocks[KGSL_MAX_CLKS]={[6]="rbbmtimer_clk"};
#include "clock-functions.h"
static void reset(bool native, unsigned int count)
{
    memset(&gpu,0,sizeof(gpu)); memset(&node,0,sizeof(node));
    platform.dev.of_node=&node; gpu.dev=&platform.dev; gpu.pdev=&platform;
    node.native=native; gpu.gmu_core.components_bound=true; gpu.gmu_core.dev_ops=&gpu;
    gpu.pwrctrl.num_pwrlevels=count; gpu.pwrctrl.power_flags=99;
    gpu.pwrctrl.active_pwrlevel=1; gpu.pwrctrl.default_pwrlevel=1;
    gpu.pwrctrl.max_pwrlevel=9; gpu.pwrctrl.thermal_pwrlevel=9;
    gpu.pwrctrl.sysfs_thermal_pwrlevel=9; gpu.pwrctrl.cooling_thermal_pwrlevel=9;
    gpu.pwrctrl.wakeup_maxpwrlevel=9;
    for(unsigned int i=0;i<KGSL_MAX_PWRLEVELS;i++) {
        gpu.pwrctrl.pwrlevels[i].gpu_freq=700000000-i*1000000;
        gpu.pwrctrl.pwrlevels[i].voltage_level=432-i;
    }
    get_calls=round_calls=set_calls=isense_calls=reg_calls=optional_clk_calls=0;
    pm_calls=timer_calls=wait_calls=get_error=reg_error=property_error=0;
    common_clk=1; round_delta=123; last_set=0;
}
static void no_cpu_ownership(void)
{ assert(!get_calls && !round_calls && !set_calls && !isense_calls && !reg_calls && !optional_clk_calls); }
static void initialized(void)
{
    assert(pm_calls==1 && timer_calls==1 && wait_calls==1);
    assert(gpu.active_cnt_wq==1 && gpu.pwrctrl.minbw_timer==1);
    assert(!gpu.pwrctrl.power_flags && !gpu.pwrctrl.max_pwrlevel && !gpu.pwrctrl.thermal_pwrlevel);
    assert(!gpu.pwrctrl.sysfs_thermal_pwrlevel && !gpu.pwrctrl.cooling_thermal_pwrlevel && !gpu.pwrctrl.wakeup_maxpwrlevel);
    assert(gpu.pwrctrl.min_pwrlevel==gpu.pwrctrl.num_pwrlevels-1);
    assert(gpu.pwrctrl.thermal_pwrlevel_floor==gpu.pwrctrl.min_pwrlevel);
    assert(gpu.pwrctrl.active_pwrlevel==1 && gpu.pwrctrl.default_pwrlevel==1);
}
int main(void)
{
    for (unsigned int count=1;count<=KGSL_MAX_PWRLEVELS;count++) {
        reset(true,count);
        struct kgsl_pwrlevel before[KGSL_MAX_PWRLEVELS]; memcpy(before,gpu.pwrctrl.pwrlevels,sizeof(before));
        node.supplies=node.bimc=true;
        assert(kgsl_pwrctrl_init(&gpu)==0); no_cpu_ownership(); initialized();
        assert(!memcmp(before,gpu.pwrctrl.pwrlevels,sizeof(before)) && gpu.pwrctrl.num_pwrlevels==count);
        scenarios++;
    }
    for (int bound=0;bound<=1;bound++) for (int ops=0;ops<=1;ops++) {
        if (bound && ops) continue;
        reset(true,10); gpu.gmu_core.components_bound=bound; gpu.gmu_core.dev_ops=ops?&gpu:NULL;
        assert(kgsl_pwrctrl_init(&gpu)==-ENODEV); no_cpu_ownership();
        assert(!pm_calls && !timer_calls && !wait_calls && gpu.pwrctrl.power_flags==99); scenarios++;
    }
    for (int i=0;i<2;i++) {
        reset(true,i?KGSL_MAX_PWRLEVELS+1:0);
        assert(kgsl_pwrctrl_init(&gpu)==-EINVAL); no_cpu_ownership(); assert(!pm_calls && !timer_calls && !wait_calls); scenarios++;
    }
    reset(true,10); node.no_nap=true;
    assert(kgsl_pwrctrl_init(&gpu)==0 && (gpu.pwrctrl.ctrl_flags&BIT(KGSL_PWRFLAGS_NAP_OFF))); no_cpu_ownership(); initialized(); scenarios++;
    reset(true,10); common_clk=0;
    assert(kgsl_pwrctrl_init(&gpu)==0 && (gpu.pwrctrl.ctrl_flags&BIT(KGSL_PWRFLAGS_NAP_OFF))); no_cpu_ownership(); initialized(); scenarios++;
    reset(false,3); node.supplies=node.bimc=true;
    assert(kgsl_pwrctrl_init(&gpu)==0); initialized();
    assert(get_calls==1 && round_calls==4 && set_calls==2 && isense_calls==1 && reg_calls==3 && optional_clk_calls==1);
    assert(gpu.pwrctrl.grp_clks[0]==&source_clk && gpu.pwrctrl.gpu_bimc_int_clk==&bimc_clk);
    assert(gpu.pwrctrl.pwrlevels[0].gpu_freq==700000123 && gpu.pwrctrl.pwrlevels[2].gpu_freq==698000123);
    assert(gpu.pwrctrl.gx_gdsc_parent_min_corner==42 && last_set==KGSL_RBBMTIMER_CLK_FREQ+round_delta); scenarios++;
    reset(false,3); get_error=-517;
    assert(kgsl_pwrctrl_init(&gpu)==-517 && get_calls==1 && !round_calls && !pm_calls); scenarios++;
    reset(false,3); node.supplies=true; reg_error=-517;
    assert(kgsl_pwrctrl_init(&gpu)==-ENODEV && reg_calls==3 && !pm_calls && !timer_calls); scenarios++;
    reset(false,3); node.supplies=true; property_error=-EINVAL;
    assert(kgsl_pwrctrl_init(&gpu)==-ENODEV && reg_calls==3 && !pm_calls && !timer_calls); scenarios++;
    printf("%d clock ownership scenarios passed; providers mocked, no hardware proof\n",scenarios);
    return 0;
}
