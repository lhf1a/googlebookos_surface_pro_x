#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <stddef.h>
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
struct work_struct {int unused;};
#define GMU_DISABLE_SLUMBER 0
typedef uint32_t u32;
#define GMU_PRIV_GPU_STARTED 0
#define GMU_PRIV_PM_SUSPEND 1
#define KGSL_STATE_NONE 0
#define KGSL_STATE_ACTIVE 1
#define KGSL_STATE_SLUMBER 2
#define KGSL_STATE_SUSPEND 3
#define KGSL_PWRFLAGS_OFF 0
#define A6XX_GBIF_HALT 0
#define oob_gpu 0
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define ADRENO_HWSCHED_FAULT_RESETTING 3
#define HZ 100
#define ADRENO_IDLE_TIMEOUT 1
#define ADRENO_HWSCHED_FAULT_RESTART 1
#define ADRENO_HWSCHED_FAULT_REPLAY 2
#define IS_ERR_OR_NULL(p) (!(p))
static bool warn_on(bool c) {return c;}
#define WARN_ON(c) warn_on(!!(c))
#define BUG_ON(c) assert(!(c))
#define dev_err(...) ((void)0)
struct a6xx_gmu_device {int power_error,num_clks;void *clks,*cx_gdsc,*pdev;unsigned long flags;};
struct kgsl_mmu {void *defaultpagetable;};
struct kgsl_device {struct work_struct idle_check_ws;struct {unsigned long flags;} gmu_core;struct kgsl_mmu mmu;struct {int transition_error;} pwrctrl;bool gmu_fault;int state,mutex,halt_gate,active_cnt,context_lock,context_idr,idle_timer;void *dev;};
struct adreno_hwsched {int fault;};
struct adreno_device {struct kgsl_device dev;struct a6xx_gmu_device gmu;struct adreno_hwsched sched;void *gpu_llc_slice,*gpuhtw_llc_slice;bool a630;};
static struct {int worker;} kgsl_driver;
#define KGSL_DEVICE(a) (&(a)->dev)
#define ADRENO_DEVICE(d) ((struct adreno_device *)(d))
static struct a6xx_gmu_device *to_a6xx_gmu(struct adreno_device *a) {return &a->gmu;}
static struct adreno_hwsched *to_hwsched(struct adreno_device *a) {return &a->sched;}
static bool test_bit(int b,unsigned long *v) {return !!(*v&(1UL<<b));}
static void set_bit(int b,unsigned long *v) {*v|=1UL<<b;}
static void clear_bit(int b,unsigned long *v) {*v&=~(1UL<<b);}
static int atomic_read(int *p) {return *p;}
static void atomic_inc(int *p) {++*p;}
static int atomic_cmpxchg(int *p,int old,int val) {int r=*p;if(r==old)*p=val;return r;}
static unsigned calls,fail_step,hard_reset,normal_off,cleanup,restarts,unhalts,replay,snapshots;
static int oob_error,active_error,idle_error,boot_error,reset_error,cx_error,root_error,record_error,context_error;
static unsigned root_restores,record_resets,retired_refs;
static bool idle;
static unsigned idle_rearms,scale_updates,oob_requests;
static int idle_interleave;
static unsigned long jiffies;
static int step(void) {++calls;return calls==fail_step?-ETIMEDOUT:0;}
static int a6xx_gmu_wait_for_lowest_idle(struct adreno_device *a) {return step();}
static int a6xx_gmu_notify_slumber(struct adreno_device *a) {return step();}
static int a6xx_hwsched_notify_slumber(struct adreno_device *a) {return step();}
static int a6xx_gmu_wait_for_idle(struct adreno_device *a) {return step();}
static int a6xx_rscc_sleep_sequence(struct adreno_device *a) {return step();}
static int a6xx_halt_gbif(struct adreno_device *a) {return step();}
static bool adreno_is_a630(struct adreno_device *a) {return a->a630;}
static void a6xx_rdpm_mx_freq_update(struct a6xx_gmu_device *g,int f) {}
static void a6xx_rdpm_cx_freq_update(struct a6xx_gmu_device *g,int f) {}
static void kgsl_regwrite(struct kgsl_device *d,int r,int v) {}
static void a6xx_gmu_irq_disable(struct adreno_device *a) {}
static void a6xx_hfi_stop(struct adreno_device *a) {}
static void a6xx_hwsched_hfi_stop(struct adreno_device *a) {}
static void clk_bulk_disable_unprepare(int n,void *c) {}
static int a6xx_cx_regulator_disable_wait(void *r,struct kgsl_device *d,int t) {++normal_off;return cx_error;}
static int a6xx_gmu_suspend(struct adreno_device *a) {++hard_reset;if(reset_error && !a->gmu.power_error)a->gmu.power_error=reset_error;return reset_error;}
static int a6xx_gmu_disable_clks(struct adreno_device *a) {clk_bulk_disable_unprepare(0,NULL);return 0;}
static int a6xx_gmu_disable_cx(struct adreno_device *a) {return a6xx_cx_regulator_disable_wait(a->gmu.cx_gdsc,&a->dev,5000);}
static void trace_kgsl_pwr_request_state(struct kgsl_device *d,int s) {}
static void trace_kgsl_pwr_set_state(struct kgsl_device *d,int s) {}
static int adreno_suspend_context(struct kgsl_device *d) {
 /* Model another owner running while dispatcher_idle releases device->mutex. */
 struct adreno_device *a=ADRENO_DEVICE(d);
 if(idle_interleave==1)d->active_cnt=1;
 if(idle_interleave==2)a->gmu.power_error=-EIO;
 if(idle_interleave==3)d->pwrctrl.transition_error=-ENODEV;
 if(idle_interleave==4)clear_bit(GMU_PRIV_GPU_STARTED,&a->gmu.flags);
 return context_error;
}
static void kgsl_start_idle_timer(struct kgsl_device *d){assert(d->mutex==1);idle_rearms++;}
static void kgsl_pwrscale_update(struct kgsl_device *d){assert(d->mutex==1);scale_updates++;}
static int a6xx_gmu_oob_set(struct kgsl_device *d,int r) {oob_requests++;return oob_error;}
static void a6xx_gmu_oob_clear(struct kgsl_device *d,int r) {}
static void kgsl_pwrscale_update_stats(struct kgsl_device *d) {}
static void adreno_coresight_stop(struct adreno_device *a) {}
static void adreno_perfcounter_save(struct adreno_device *a) {}
static void a630_vbif_halt(struct adreno_device *a) {}
static void adreno_irqctrl(struct adreno_device *a,int on) {}
static void kgsl_pwrctrl_irq(struct kgsl_device *d,int on) {}
static void adreno_set_active_ctxs_null(struct adreno_device *a) {++cleanup;}
static void adreno_dispatcher_stop(struct adreno_device *a) {++cleanup;}
static void adreno_ringbuffer_stop(struct adreno_device *a) {++cleanup;}
static void llcc_slice_deactivate(void *p) {++cleanup;}
static void timer_delete_sync(int *p) {++cleanup;}
static void kgsl_pwrscale_sleep(struct kgsl_device *d) {++cleanup;}
static void kgsl_pwrctrl_clear_l3_vote(struct kgsl_device *d) {++cleanup;}
static void adreno_profile_process_results(struct adreno_device *a) {}
static bool a6xx_hw_isidle(struct adreno_device *a) {return idle;}
static void read_lock(int *p) {}
static void read_unlock(int *p) {}
static int unregister_context_hwsched;
static void idr_for_each(int *p,int fn,void *d) {++cleanup;}
static void reinit_completion(int *p) {*p=0;}
static int kgsl_active_count_wait(struct kgsl_device *d,int n,int t) {return active_error;}
static int adreno_idle(struct kgsl_device *d) {return idle_error;}
static void adreno_dispatcher_halt(struct kgsl_device *d) {}
static void adreno_dispatcher_start(struct kgsl_device *d) {++unhalts;}
static void adreno_dispatcher_unhalt(struct kgsl_device *d) {++unhalts;}
static void adreno_hwsched_start(struct adreno_device *a) {++unhalts;}
static void mutex_lock(int *p) {*p=1;}
static void mutex_unlock(int *p) {*p=0;}
static bool mutex_is_locked(int *p) {return *p;}
static void kthread_flush_worker(int *p) {}
static int msecs_to_jiffies(int n) {return n;}
static bool time_before(unsigned long a,unsigned long b) {++jiffies;return a<b;}
static int a6xx_boot(struct adreno_device *a) {++restarts;return boot_error;}
static int a6xx_hwsched_boot(struct adreno_device *a) {++restarts;return boot_error;}
static void trace_kgsl_active_count(struct kgsl_device *d,unsigned long p) {}
static void a6xx_disable_gpu_irq(struct adreno_device *a) {}
static void a6xx_hwsched_drain_ctxt_unregister(struct adreno_device *a) {}
static int reset_and_snapshot(struct adreno_device *a);
static int kgsl_mmu_set_pt(struct kgsl_mmu *m,void *p){assert((hard_reset&&!reset_error)||(normal_off&&!cx_error));root_restores++;return root_error;}
static int a6xx_preemption_reset_records(struct adreno_device *a){assert(root_restores&&!root_error);record_resets++;return record_error;}
static void adreno_drawctxt_retire_switches(struct adreno_device *a){assert(record_resets&&!record_error);retired_refs++;}
static void adreno_hwsched_init_replay(struct adreno_device *a){}
static int adreno_hwsched_complete_replay(struct adreno_device *a) {++replay;return 0;}
#include "kgsl-power-off-functions.h"
static int reset_and_snapshot(struct adreno_device *a) {++snapshots;return a6xx_hwsched_restart(a);}
static struct adreno_device reset(void) {
 calls=fail_step=hard_reset=normal_off=cleanup=restarts=unhalts=replay=snapshots=0;
 oob_error=active_error=idle_error=boot_error=reset_error=cx_error=root_error=record_error=context_error=0;root_restores=record_resets=retired_refs=0;idle=true;jiffies=0;idle_rearms=scale_updates=oob_requests=0;idle_interleave=0;
 return (struct adreno_device){.dev={.state=KGSL_STATE_ACTIVE,.mutex=1,.halt_gate=1},.gmu={.flags=1},.gpu_llc_slice=(void *)1,.gpuhtw_llc_slice=(void *)2};
}
int main(void) {
 unsigned cases=0;
 for(int hw=0;hw<2;++hw) {
  int (*low)(struct adreno_device *)=hw?a6xx_hwsched_gmu_power_off:a6xx_gmu_power_off;
  int (*upper)(struct adreno_device *)=hw?a6xx_hwsched_power_off:a6xx_power_off;
  int (*pm)(struct adreno_device *)=hw?a6xx_hwsched_pm_suspend:a6xx_gmu_pm_suspend;
  int (*get)(struct adreno_device *)=hw?a6xx_hwsched_active_count_get:a6xx_gmu_active_count_get;
  for(unsigned err=0;err<=6;++err) {
   struct adreno_device a=reset();fail_step=err;a.dev.gmu_fault=err==6;int want=err==6?-EIO:err?-ETIMEDOUT:0;
   assert(low(&a)==want);
   assert(calls==(err==6?0:err?err:5));
   assert(normal_off==!err && hard_reset==!!err && a.gmu.power_error==want);
   if(err){unsigned before=calls;assert(low(&a)==want && calls==before && hard_reset==1);assert(upper(&a)==want && !cleanup);assert(get(&a)==want && !a.dev.active_cnt && !restarts);}
   ++cases;
  }
  for(unsigned err=0;err<=5;++err) {
   struct adreno_device a=reset();fail_step=err;
   assert(upper(&a)==(err?-ETIMEDOUT:0));
   assert(test_bit(GMU_PRIV_GPU_STARTED,&a.gmu.flags)==!!err);
   assert(err?!cleanup:cleanup>0);++cases;
  }
  for(unsigned err=0;err<=5;++err) {
   struct adreno_device a=reset();fail_step=err;
   assert(pm(&a)==(err?-ETIMEDOUT:0));
   assert(test_bit(GMU_PRIV_PM_SUSPEND,&a.gmu.flags)==!err);
   assert(a.dev.halt_gate==0 && a.dev.mutex==1 && !unhalts);
   if(err){unsigned before=calls;assert(pm(&a)==-ETIMEDOUT && calls==before);}
   ++cases;
  }
  {struct adreno_device a=reset();oob_error=-EIO;assert(upper(&a)==-EIO && !test_bit(GMU_PRIV_GPU_STARTED,&a.gmu.flags) && !a.gmu.power_error);++cases;}
  {struct adreno_device a=reset();active_error=-EBUSY;assert(pm(&a)==-EBUSY && unhalts && !calls && !a.gmu.power_error);++cases;}
  {struct adreno_device a=reset();a.gmu.power_error=-EIO;assert(get(&a)==-EIO && !restarts && !a.dev.active_cnt);++cases;}
 }
 {struct adreno_device a=reset();a.a630=true;assert(!a6xx_gmu_power_off(&a) && calls==4);++cases;}
 {struct adreno_device a=reset();a.gmu.power_error=-EIO;assert(a6xx_gmu_restart(&a.dev)==-EIO && !hard_reset && !restarts);++cases;}
 {struct adreno_device a=reset();a.gmu.power_error=-EIO;assert(a6xx_hwsched_restart(&a)==-EIO && !hard_reset && !restarts && !cleanup);++cases;}
 {struct adreno_device a=reset();boot_error=-EIO;a.sched.fault=ADRENO_HWSCHED_FAULT_RESTART;adreno_hwsched_recovery(&a);assert(a.dev.pwrctrl.transition_error==-EIO && !replay && snapshots==1);adreno_hwsched_recovery(&a);assert(!replay && snapshots==1 && a.dev.mutex==0);++cases;}
 for(int hw=0;hw<2;++hw) {
  struct adreno_device a=reset();reset_error=-EBUSY;
  assert((hw?a6xx_hwsched_restart(&a):a6xx_gmu_restart(&a.dev))==-EBUSY);
  assert(a.gmu.power_error==-EBUSY && test_bit(GMU_PRIV_GPU_STARTED,&a.gmu.flags) && !restarts && !cleanup);++cases;
  a=reset();cx_error=-EIO;
  assert((hw?a6xx_hwsched_power_off(&a):a6xx_power_off(&a))==-EIO);
  assert(a.gmu.power_error==-EIO && test_bit(GMU_PRIV_GPU_STARTED,&a.gmu.flags) && !cleanup);++cases;
 }
 {struct adreno_device a=reset();assert(!a6xx_gmu_restart(&a.dev)&&hard_reset==1&&root_restores==1&&record_resets==1&&retired_refs==1&&restarts==1);++cases;}
 {struct adreno_device a=reset();root_error=-ETIMEDOUT;assert(a6xx_gmu_restart(&a.dev)==-ETIMEDOUT&&root_restores==1&&!record_resets&&!retired_refs&&!restarts);++cases;}
 {struct adreno_device a=reset();record_error=-EINVAL;assert(a6xx_gmu_restart(&a.dev)==-EINVAL&&record_resets==1&&!retired_refs&&!restarts);++cases;}
 {struct adreno_device a=reset();context_error=-EBUSY;assert(a6xx_power_off(&a)==-EBUSY&&!calls&&!normal_off&&!root_restores&&!retired_refs&&!cleanup&&a.gmu.flags==1);context_error=0;assert(!a6xx_power_off(&a)&&root_restores==1&&record_resets==1&&retired_refs==1&&!a.gmu.flags);++cases;}
 {struct adreno_device a=reset();root_error=-EIO;assert(a6xx_power_off(&a)==-EIO&&normal_off==1&&root_restores==1&&!record_resets&&!retired_refs&&!cleanup&&!a.gmu.flags&&a.gmu.power_error==-EIO&&a.dev.pwrctrl.transition_error==-EIO);unsigned prior=calls;assert(a6xx_gmu_active_count_get(&a)==-EIO&&!a.dev.active_cnt&&!restarts&&calls==prior);assert(a6xx_power_off(&a)==-EIO&&calls==prior);++cases;}
 {struct adreno_device a=reset();record_error=-EINVAL;assert(a6xx_power_off(&a)==-EINVAL&&normal_off==1&&record_resets==1&&!retired_refs&&!cleanup&&!a.gmu.flags&&a.gmu.power_error==-EINVAL&&a.dev.pwrctrl.transition_error==-EINVAL);++cases;}
 {struct adreno_device a=reset();root_error=-ETIMEDOUT;assert(a6xx_gmu_pm_suspend(&a)==-ETIMEDOUT&&!test_bit(GMU_PRIV_PM_SUSPEND,&a.gmu.flags)&&!a.gmu.flags&&!unhalts&&!retired_refs&&!cleanup);++cases;}
 {struct adreno_device a=reset();record_error=-EINVAL;assert(a6xx_gmu_pm_suspend(&a)==-EINVAL&&!test_bit(GMU_PRIV_PM_SUSPEND,&a.gmu.flags)&&!unhalts&&!retired_refs&&!cleanup);++cases;}
 {struct adreno_device a=reset();a.dev.pwrctrl.transition_error=-EIO;root_error=-ETIMEDOUT;normal_off=1;assert(a6xx_gmu_retire_contexts(&a)==-ETIMEDOUT&&a.gmu.power_error==-ETIMEDOUT&&a.dev.pwrctrl.transition_error==-EIO&&!retired_refs);++cases;}
 {struct adreno_device a=reset();assert(!a6xx_gmu_pm_suspend(&a)&&retired_refs==1&&record_resets==1&&root_restores==1&&test_bit(GMU_PRIV_PM_SUSPEND,&a.gmu.flags));++cases;}
 {struct adreno_device a=reset();assert(!a6xx_power_off(&a)&&retired_refs==1&&record_resets==1);assert(!a6xx_gmu_active_count_get(&a)&&restarts==1&&a.dev.active_cnt==1);++cases;}
 /* Idle worker retries without a new active-count put; fatal/off states never retry. */
 {struct adreno_device a=reset();context_error=-ETIMEDOUT;gmu_idle_check(&a.dev.idle_check_ws);assert(idle_rearms==1&&!scale_updates&&!calls&&!normal_off&&!cleanup&&a.gmu.flags==1&&!a.dev.mutex);context_error=0;gmu_idle_check(&a.dev.idle_check_ws);assert(idle_rearms==1&&retired_refs==1&&!a.gmu.flags&&!a.dev.mutex);++cases;}
 for(int err=1;err<=4;err++) {
  struct adreno_device a=reset();idle_interleave=err;gmu_idle_check(&a.dev.idle_check_ws);
  assert(!calls&&!oob_requests&&!normal_off&&!retired_refs&&!cleanup&&!a.dev.mutex);
  assert(idle_rearms==(err==1));
  assert(err!=1||a.dev.active_cnt==1);
  assert(err!=2||a.gmu.power_error==-EIO);
  assert(err!=3||a.dev.pwrctrl.transition_error==-ENODEV);
  assert(err!=4||!a.gmu.flags);++cases;
 }
 {struct adreno_device a=reset();a.dev.active_cnt=1;gmu_idle_check(&a.dev.idle_check_ws);assert(idle_rearms==1&&scale_updates==1&&!calls&&!normal_off&&!cleanup&&!a.dev.mutex);++cases;}
 {struct adreno_device a=reset();a.gmu.flags=0;gmu_idle_check(&a.dev.idle_check_ws);assert(!idle_rearms&&!scale_updates&&!calls&&!normal_off&&!cleanup&&!a.dev.mutex);++cases;}
 {struct adreno_device a=reset();a.dev.gmu_core.flags=1;gmu_idle_check(&a.dev.idle_check_ws);assert(!idle_rearms&&!scale_updates&&!calls&&!normal_off&&!cleanup&&!a.dev.mutex);++cases;}
 for(int active=0;active<=1;active++)for(int fault=0;fault<=1;fault++) {
  struct adreno_device a=reset();a.dev.active_cnt=active;
  if(fault)a.gmu.power_error=-EIO;else a.dev.pwrctrl.transition_error=-EINVAL;
  gmu_idle_check(&a.dev.idle_check_ws);assert(!idle_rearms&&!scale_updates&&!calls&&!normal_off&&!cleanup&&!a.dev.mutex);++cases;
 }
 for(int err=1;err<=4;err++) {
  struct adreno_device a=reset();if(err==1)fail_step=1;if(err==2)root_error=-EIO;if(err==3)record_error=-EINVAL;if(err==4)oob_error=-EIO;
  gmu_idle_check(&a.dev.idle_check_ws);assert(!idle_rearms&&!a.dev.mutex);assert(err!=4||(!a.gmu.flags&&retired_refs==1));++cases;
 }
 {struct adreno_device a=reset();idle_interleave=1;assert(a6xx_gmu_pm_suspend(&a)==-EBUSY&&!test_bit(GMU_PRIV_PM_SUSPEND,&a.gmu.flags)&&!calls&&!normal_off&&!retired_refs&&unhalts==1);++cases;}
 {struct adreno_device a=reset();a.dev.pwrctrl.transition_error=-ENODEV;assert(a6xx_power_off(&a)==-ENODEV&&!calls&&!normal_off&&!retired_refs);++cases;}
 {struct adreno_device a=reset();context_error=-ETIMEDOUT;assert(a6xx_gmu_pm_suspend(&a)==-ETIMEDOUT&&!test_bit(GMU_PRIV_PM_SUSPEND,&a.gmu.flags)&&!calls&&!normal_off&&!retired_refs&&unhalts==1);++cases;}
 for(int err=2;err<=3;err++){struct adreno_device a=reset();idle_interleave=err;assert(a6xx_gmu_pm_suspend(&a)==(err==2?-EIO:-ENODEV)&&!test_bit(GMU_PRIV_PM_SUSPEND,&a.gmu.flags)&&!calls&&!oob_requests&&!normal_off&&!retired_refs&&!unhalts);++cases;}
 printf("%u actual shutdown/caller/PM/recovery scenarios passed; power/reset/MMIO mocked\n",cases);return 0;
}
