/* Actual provider consumers and state transitions; hardware dependencies mocked. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
typedef uint32_t u32;
#define BIT(n) (1U<<(n))
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define fallthrough __attribute__((fallthrough))
#define __iomem
#define dev_warn(dev,...) ((void)(dev))
#define dev_err(dev,...) ((void)(dev))
#define KGSL_MAX_CLKS 4
#define HZ 100
#define NUM_TIMES_RESET_RETRY 2
#define ADRENO_IOMMU_PAGE_FAULT 4
#define ADRENO_DEVICE_GPU_REGULATOR_ENABLED 0
#define ADRENO_DEVICE_GPMU_INITIALIZED 1
#define ADRENO_DEVICE_STARTED 2
#define KGSL_BUS_VOTE_ON 1
#define KGSL_PWRFLAGS_OFF 0
#define KGSL_PWRFLAGS_ON 1
enum {KGSL_STATE_NONE,KGSL_STATE_INIT,KGSL_STATE_ACTIVE,KGSL_STATE_SLUMBER,KGSL_STATE_AWARE,KGSL_STATE_NAP,KGSL_STATE_MINBW,KGSL_STATE_SUSPEND};
enum {CLKFLAG_RETAIN_PERIPH,CLKFLAG_NORETAIN_PERIPH,CLKFLAG_RETAIN_MEM,CLKFLAG_NORETAIN_MEM};
enum {A5XX_GPMU_SP_POWER_CNTL,A5XX_GPMU_SP_PWR_CLK_STATUS,A5XX_GPMU_RBCCU_POWER_CNTL,A5XX_GPMU_RBCCU_PWR_CLK_STATUS,A5XX_GPMU_RBCCU_CLOCK_CNTL,A5XX_RBBM_BLOCK_SW_RESET_CMD,A6XX_GPU_CC_GX_GDSCR};
#define A6XX_GMU_SPTPRAC_PWR_CLK_STATUS 1
#define A6XX_GMU_GX_SPTPRAC_POWER_CONTROL 2
#define A6XX_RETAIN_FF_ENABLE_ENABLE_MASK BIT(3)
#define SPTPRAC_POWEROFF_CTRL_MASK 7
#define SPTPRAC_POWEROFF_STATUS_MASK 3
struct clk {int n;};
struct kgsl_device;struct adreno_device;
struct adreno_gpudev {
    int (*regulator_disable)(struct adreno_device *);
    int (*clk_set_options)(struct adreno_device *,const char *,struct clk *,bool);
    int (*reset)(struct kgsl_device *);
};
struct adreno_power_ops {int (*first_open)(struct adreno_device *);int (*active_count_get)(struct adreno_device *);};
struct kgsl_functable {
    int (*regulator_disable)(struct kgsl_device *);
    int (*clk_set_options)(struct kgsl_device *,const char *,struct clk *,bool);
    int (*stop)(struct kgsl_device *),(*suspend_context)(struct kgsl_device *),(*start)(struct kgsl_device *,int);
    bool (*prepare_for_power_off)(struct kgsl_device *);
    void (*resume)(struct kgsl_device *),(*deassert_gbif_halt)(struct kgsl_device *);
    int (*drain)(struct kgsl_device *),(*idle)(struct kgsl_device *);
};
struct kgsl_pwrctrl {
    struct clk *grp_clks[KGSL_MAX_CLKS];int transition_error,active_pwrlevel,previous_pwrlevel;
    struct {unsigned gpu_freq;} pwrlevels[2];int minbw_timer;bool superfast;long last_stat_updated;
};
struct kgsl_device {struct kgsl_pwrctrl pwrctrl;struct kgsl_functable *ftbl;int state,requested_state,idle_timer,hwaccess_gate,active_cnt;void *dev;};
struct adreno_device {struct kgsl_device dev;struct adreno_gpudev *gpudev;struct adreno_power_ops *power_ops;unsigned long priv;int model;bool sptp_pc_enabled;char *gmu_wrapper_virt;unsigned gmu_wrapper_base;};
#define KGSL_DEVICE(a) (&(a)->dev)
#define ADRENO_DEVICE(d) ((struct adreno_device *)(d))
#define ADRENO_GPU_DEVICE(a) ((a)->gpudev)
#define ADRENO_POWER_OPS(a) ((a)->power_ops)
#define MODEL(n) static bool adreno_is_a##n(struct adreno_device *a){return a->model==n;}
MODEL(306) MODEL(540) MODEL(512) MODEL(508) MODEL(530)
#define adreno_is_a306a adreno_is_a306
static bool test_bit(int b,unsigned long *v){return !!(*v&(1UL<<b));}
static void clear_bit(int b,unsigned long *v){*v&=~(1UL<<b);}
static int atomic_read(int *p){return *p;}
static struct adreno_device gpu;
static struct adreno_gpudev gpudev;
static struct adreno_power_ops power_ops;
static struct kgsl_functable ftbl;
static struct clk clocks_obj[4];
static const char *clocks[KGSL_MAX_CLKS]={"unused","mem_iface_clk","core_clk","other"};
static int flag_calls,flag_fail,flags_seen[16],reg_error,reg_calls,enable_error,stop_error,context_error,start_error,bus_error,rail_error,wait_error,idle_error;
static int stopped,started,resumed,context_calls,bus_calls,clock_off,rail_calls,l3_calls,state_calls,sleep_calls,open_calls,active_calls,soft_error,reset_calls;
static int gdsc_calls,gdsc_fail,mmio_writes,holi_error,scenarios;
static unsigned reg_values[8];static bool can_stop;
static int qcom_clk_set_flags(struct clk *c,unsigned long f){assert(c);flags_seen[flag_calls++]=f;return flag_calls==flag_fail?-EIO:0;}
static int _poll_gdsc_status(struct adreno_device *a,unsigned r,unsigned v){gdsc_calls++;return gdsc_calls==gdsc_fail?-ETIMEDOUT:0;}
static void kgsl_regwrite(struct kgsl_device *d,unsigned r,unsigned v){mmio_writes++;}
static void kgsl_regread(struct kgsl_device *d,unsigned r,unsigned *v){*v=reg_values[r];}
static void udelay(int n){}
static void adreno_read_gmu_wrapper(struct adreno_device *a,unsigned r,u32 *v){*v=reg_values[r];}
static void adreno_write_gmu_wrapper(struct adreno_device *a,unsigned r,u32 v){mmio_writes++;}
#define readl_poll_timeout(addr,val,condition,delay,timeout) ((void)(addr),(void)(condition),holi_error)
static int mock_regulator(struct adreno_device *a){reg_calls++;return reg_error;}
static int kgsl_pwrctrl_axi(struct kgsl_device *d,int state){bus_calls++;return bus_error;}
static void kgsl_pwrctrl_clear_l3_vote(struct kgsl_device *d){l3_calls++;}
static void kgsl_pwrctrl_clk(struct kgsl_device *d,int state,int target){if(!state)clock_off++;}
static int kgsl_pwrctrl_pwrrail(struct kgsl_device *d,int state){rail_calls++;return rail_error;}
static void kgsl_pwrctrl_request_state(struct kgsl_device *d,int state){d->requested_state=state;}
static void kgsl_pwrctrl_set_state(struct kgsl_device *d,int state){d->state=state;state_calls++;}
static int kgsl_pwrctrl_enable(struct kgsl_device *d){return enable_error;}
static void timer_delete_sync(int *p){}
static void kgsl_pwrctrl_irq(struct kgsl_device *d,int v){}
static void kgsl_pwrscale_midframe_timer_cancel(struct kgsl_device *d){}
static void complete_all(int *p){}
static void kgsl_pwrscale_wake(struct kgsl_device *d){}
static void trace_gpu_frequency(unsigned f,int n){}
static int kgsl_bus_update(struct kgsl_device *d,int state){return 0;}
static long ktime_get(void){return 1;}
static void kgsl_pwrctrl_pwrlevel_change_settings(struct kgsl_device *d,int n){}
static void kgsl_start_idle_timer(struct kgsl_device *d){}
static void kgsl_pwrscale_sleep(struct kgsl_device *d){sleep_calls++;}
static int kgsl_active_count_wait(struct kgsl_device *d,int c,int timeout){return wait_error;}
static int _aware(struct kgsl_device *d){return 0;}
static int _nap(struct kgsl_device *d){return 0;}
static int _minbw(struct kgsl_device *d){return 0;}
static int kgsl_pwrctrl_change_state(struct kgsl_device *d,int state);
static int adreno_soft_reset(struct kgsl_device *d){reset_calls++;return soft_error;}
static int adreno_start(struct kgsl_device *d,int p){started++;return start_error;}
static void msleep(int n){}
#include "kgsl-power-callback-functions.h"
static int mock_stop(struct kgsl_device *d){stopped++;return stop_error;}
static int mock_context(struct kgsl_device *d){context_calls++;return context_error;}
static int mock_start(struct kgsl_device *d,int p){started++;return start_error;}
static void mock_resume(struct kgsl_device *d){resumed++;}
static void mock_deassert(struct kgsl_device *d){}
static bool mock_prepare(struct kgsl_device *d){return can_stop;}
static int mock_drain(struct kgsl_device *d){return 0;}
static int mock_idle(struct kgsl_device *d){return idle_error;}
static int mock_open(struct adreno_device *a){open_calls++;return 0;}
static int mock_active(struct adreno_device *a){active_calls++;return 0;}
static void setup(void)
{
    memset(&gpu,0,sizeof(gpu));memset(&gpudev,0,sizeof(gpudev));memset(&ftbl,0,sizeof(ftbl));memset(reg_values,0,sizeof(reg_values));
    gpu.gpudev=&gpudev;gpu.power_ops=&power_ops;gpu.dev.ftbl=&ftbl;gpu.dev.state=KGSL_STATE_ACTIVE;gpu.dev.requested_state=99;gpu.model=540;
    gpu.priv=BIT(ADRENO_DEVICE_GPU_REGULATOR_ENABLED);gpu.gmu_wrapper_virt=(char *)reg_values;
    gpudev.clk_set_options=a6xx_clk_set_options;gpudev.regulator_disable=mock_regulator;
    ftbl.regulator_disable=adreno_regulator_disable;ftbl.clk_set_options=adreno_clk_set_options;ftbl.stop=mock_stop;ftbl.start=mock_start;ftbl.suspend_context=mock_context;
    ftbl.resume=mock_resume;ftbl.deassert_gbif_halt=mock_deassert;ftbl.prepare_for_power_off=mock_prepare;ftbl.drain=mock_drain;ftbl.idle=mock_idle;
    power_ops.first_open=mock_open;power_ops.active_count_get=mock_active;
    gpu.dev.pwrctrl.grp_clks[1]=&clocks_obj[1];gpu.dev.pwrctrl.grp_clks[2]=&clocks_obj[2];
    flag_calls=flag_fail=reg_error=reg_calls=enable_error=stop_error=context_error=start_error=bus_error=rail_error=wait_error=idle_error=0;
    stopped=started=resumed=context_calls=bus_calls=clock_off=rail_calls=l3_calls=state_calls=sleep_calls=open_calls=active_calls=reset_calls=0;
    gdsc_calls=gdsc_fail=mmio_writes=holi_error=0;soft_error=-EIO;can_stop=true;
}
static void latched(int error)
{
    assert(gpu.dev.pwrctrl.transition_error==error && gpu.dev.requested_state==KGSL_STATE_NONE);
    int state=gpu.dev.state,calls=state_calls;
    assert(kgsl_pwrctrl_change_state(&gpu.dev,state)==error && state_calls==calls);
    assert(adreno_first_open(&gpu.dev)==error && !open_calls);
    assert(adreno_active_count_get(&gpu)==error && !active_calls);
    assert(adreno_reset(&gpu.dev,0)==error && !reset_calls);
}
int main(void)
{
    int (*callbacks[])(struct adreno_device *,const char *,struct clk *,bool)={a3xx_clk_set_options,a5xx_clk_set_options,a6xx_clk_set_options};
    for(int kind=0;kind<3;kind++)for(int failure=0;failure<=2;failure++) {
        setup();gpu.model=kind==0?306:540;flag_fail=failure;
        int r=callbacks[kind](&gpu,"core_clk",&clocks_obj[2],true);
        assert(r==(failure?-EIO:0) && flag_calls==(failure?failure:2));
        assert(flags_seen[0]==CLKFLAG_RETAIN_PERIPH);if(flag_calls==2)assert(flags_seen[1]==CLKFLAG_RETAIN_MEM);scenarios++;
    }
    setup();assert(!a6xx_clk_set_options(&gpu,"core_clk",&clocks_obj[2],false));assert(flag_calls==2 && flags_seen[0]==CLKFLAG_NORETAIN_PERIPH && flags_seen[1]==CLKFLAG_NORETAIN_MEM);scenarios++;
    setup();assert(!a6xx_clk_set_options(&gpu,"mem_iface_clk",&clocks_obj[1],true));assert(flag_calls==2 && flags_seen[0]==CLKFLAG_NORETAIN_PERIPH && flags_seen[1]==CLKFLAG_NORETAIN_MEM);scenarios++;
    setup();assert(!a3xx_clk_set_options(&gpu,"core_clk",&clocks_obj[2],true) && !flag_calls);gpu.model=600;assert(!a5xx_clk_set_options(&gpu,"core_clk",&clocks_obj[2],true) && !flag_calls);scenarios++;
    setup();assert(!adreno_clk_set_options(&gpu.dev,"other",&clocks_obj[3],true) && !flag_calls);gpudev.clk_set_options=NULL;assert(!adreno_clk_set_options(&gpu.dev,"core_clk",&clocks_obj[2],true) && !flag_calls);scenarios++;
    for(int failure=1;failure<=4;failure++) {
        setup();flag_fail=failure;assert(kgsl_pwrctrl_clk_set_options(&gpu.dev,true)==-EIO);assert(flag_calls==failure);latched(-EIO);scenarios++;
    }
    setup();assert(!adreno_regulator_disable(&gpu.dev) && reg_calls==1 && !gpu.priv);assert(!adreno_regulator_disable(&gpu.dev) && reg_calls==1);scenarios++;
    setup();reg_error=-ETIMEDOUT;assert(adreno_regulator_disable(&gpu.dev)==-ETIMEDOUT && (gpu.priv&1));scenarios++;
    for(int failure=1;failure<=2;failure++) {
        setup();gdsc_fail=failure;assert(a5xx_regulator_disable(&gpu)==-ETIMEDOUT && gdsc_calls==failure);scenarios++;
    }
    for(int i=0;i<3;i++) {
        setup();gpu.sptp_pc_enabled=true;gpu.priv|=BIT(ADRENO_DEVICE_GPMU_INITIALIZED);
        unsigned regs[]={A5XX_GPMU_SP_PWR_CLK_STATUS,A5XX_GPMU_RBCCU_PWR_CLK_STATUS,A5XX_GPMU_RBCCU_CLOCK_CNTL};
        reg_values[regs[i]]=i==2?BIT(14):BIT(20);assert(a5xx_regulator_disable(&gpu)==-EBUSY);scenarios++;
    }
    setup();assert(!a5xx_regulator_disable(&gpu) && gdsc_calls==2);scenarios++;
    setup();gpu.model=512;assert(!a5xx_regulator_disable(&gpu) && !gdsc_calls && !mmio_writes);scenarios++;
    setup();holi_error=-ETIMEDOUT;assert(a6xx_holi_gmu_sptprac_disable(&gpu)==-ETIMEDOUT && mmio_writes==2);scenarios++;
    setup();assert(!a6xx_holi_gmu_sptprac_disable(&gpu) && mmio_writes==2);scenarios++;
    setup();reg_error=-EIO;assert(kgsl_pwrctrl_disable(&gpu.dev)==-EIO && !bus_calls && !clock_off && !rail_calls && !l3_calls && (gpu.priv&1));latched(-EIO);scenarios++;
    setup();bus_error=-EAGAIN;assert(kgsl_pwrctrl_disable(&gpu.dev)==-EAGAIN && !clock_off && !rail_calls && !l3_calls);latched(-EAGAIN);scenarios++;
    setup();assert(!kgsl_pwrctrl_disable(&gpu.dev) && bus_calls==1 && clock_off==1 && rail_calls==1 && l3_calls==1);scenarios++;
    for(int stage=0;stage<6;stage++) {
        setup();if(stage==0)enable_error=-EIO;if(stage==1)context_error=-EIO;if(stage==2)stop_error=-EIO;if(stage==3)flag_fail=1;if(stage==4)reg_error=-EIO;if(stage==5)bus_error=-EIO;
        assert(kgsl_pwrctrl_change_state(&gpu.dev,KGSL_STATE_SLUMBER)==-EIO);assert(gpu.dev.state==KGSL_STATE_ACTIVE && !state_calls && !clock_off && !sleep_calls);latched(-EIO);scenarios++;
    }
    setup();rail_error=-EIO;assert(kgsl_pwrctrl_change_state(&gpu.dev,KGSL_STATE_SLUMBER)==-EIO && !state_calls && !sleep_calls);latched(-EIO);scenarios++;
    setup();assert(!kgsl_pwrctrl_change_state(&gpu.dev,KGSL_STATE_SLUMBER) && gpu.dev.state==KGSL_STATE_SLUMBER && clock_off==1 && sleep_calls==1);scenarios++;
    for(int state=KGSL_STATE_SLUMBER;state<=KGSL_STATE_AWARE;state++) {
        setup();gpu.dev.state=state;flag_fail=1;assert(kgsl_pwrctrl_change_state(&gpu.dev,KGSL_STATE_ACTIVE)==-EIO && !started && !state_calls);latched(-EIO);scenarios++;
    }
    setup();gpu.dev.state=KGSL_STATE_NAP;enable_error=-EIO;assert(kgsl_pwrctrl_change_state(&gpu.dev,KGSL_STATE_INIT)==-EIO && !stopped && !state_calls);latched(-EIO);scenarios++;
    setup();stop_error=-EIO;assert(kgsl_pwrctrl_change_state(&gpu.dev,KGSL_STATE_INIT)==-EIO && !reg_calls && !state_calls);latched(-EIO);scenarios++;
    setup();reg_error=-EIO;assert(kgsl_pwrctrl_change_state(&gpu.dev,KGSL_STATE_SUSPEND)==-EIO && !resumed && !state_calls);latched(-EIO);scenarios++;
    setup();wait_error=-EBUSY;assert(kgsl_pwrctrl_change_state(&gpu.dev,KGSL_STATE_SUSPEND)==-EBUSY && resumed==1 && !gpu.dev.pwrctrl.transition_error);scenarios++;
    setup();assert(!adreno_first_open(&gpu.dev) && !adreno_active_count_get(&gpu) && open_calls==1 && active_calls==1);scenarios++;
    setup();reg_error=-EIO;assert(adreno_reset(&gpu.dev,ADRENO_IOMMU_PAGE_FAULT)==-EIO && !started && (gpu.priv&1));scenarios++;
    printf("%d callback/state scenarios passed; hardware and unaffected helpers mocked\n",scenarios);
    return 0;
}
