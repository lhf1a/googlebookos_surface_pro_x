#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
typedef uint32_t u32;typedef long ktime_t;
#define BIT(n) (1U<<(n))
#define IS_ERR_OR_NULL(p) (!(p)||(uintptr_t)(p)>=(uintptr_t)-4095)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define REGULATOR_MODE_IDLE 0
#define REGULATOR_MODE_NORMAL 1
#define KGSL_STATE_NONE 0
#define ADRENO_QUIRK_CX_GDSC 1
#define ADRENO_QUIRK(a,q) ((a)->quirk)
#define A6XX_RETAIN_FF_ENABLE_ENABLE_MASK 1
#define SPTPRAC_POWEROFF_CTRL_MASK 1
#define SPTPRAC_POWEROFF_STATUS_MASK 4
#define SPTPRAC_CTRL_TIMEOUT 10
#define GPU_RESET_TIMEOUT 100
#define A6XX_GBIF_GX_HALT_MASK 4
#define A6XX_GBIF_CLIENT_HALT_MASK 1
#define A6XX_GBIF_ARB_HALT_MASK 2
#define dev_err(...) ((void)0)
enum {A6XX_GPU_CC_CX_GDSCR,A6XX_RSCC_TCS0_DRV0_STATUS,A6XX_RSCC_TCS1_DRV0_STATUS,A6XX_RSCC_TCS2_DRV0_STATUS,A6XX_RSCC_TCS3_DRV0_STATUS,A6XX_GPU_CC_GX_GDSCR,A6XX_GMU_GX_SPTPRAC_POWER_CONTROL,A6XX_GMU_SPTPRAC_PWR_CLK_STATUS,A6XX_GMU_AHB_FENCE_STATUS_CLR,A6XX_GMU_AO_AHB_FENCE_CTRL,A6XX_GMU_CM3_SYSRESET,A6XX_RBBM_GBIF_HALT,A6XX_RBBM_GBIF_HALT_ACK,A6XX_GBIF_HALT,A6XX_GBIF_HALT_ACK,A6XX_RBBM_SW_RESET_CMD,A6XX_GPU_CPR_FSM_CTL};
struct regulator {int id;};
struct kgsl_device {void *dev;int state;};
struct a6xx_gmu_device {bool native_domains,cx_held,gx_held;void *cxpd,*gxpd;int power_error,num_clks;void *clks;struct regulator *cx_gdsc,*gx_gdsc;};
struct adreno_device {struct kgsl_device device;struct a6xx_gmu_device gmu;bool quirk,holi,sptprac,gbif,a660;};
#define KGSL_DEVICE(a) (&(a)->device)
#define ADRENO_DEVICE(d) ((struct adreno_device *)(d))
static struct a6xx_gmu_device *to_a6xx_gmu(struct adreno_device *a) {return &a->gmu;}
static struct regulator cx={1},gx={2};
static int cx_disable_error,gx_enable_error,gx_disable_error,mode_error[2],spt_error,rpm_error_at,halt_error_at;
static unsigned rpm_calls,halt_calls,writes,clk_off,rdpm,enable_calls,disable_calls[3],mode_calls,wrapper_reads,cx_reads;
static bool gx_on,cx_on,stuck_gx;
static long now;
static int regulator_enable(struct regulator *r) {++enable_calls;return r==&gx?gx_enable_error:0;}
static int regulator_disable(struct regulator *r) {++disable_calls[r->id];if(r==&cx)return cx_disable_error;if(!gx_disable_error && !stuck_gx)gx_on=false;return gx_disable_error;}
static int regulator_set_mode(struct regulator *r,int mode) {++mode_calls;return mode_error[mode];}
static ktime_t ktime_get(void) {return now++;}
static ktime_t ktime_add_us(ktime_t t,long us) {return t+us;}
static int ktime_compare(ktime_t a,ktime_t b) {return a>b?1:a<b?-1:0;}
static void usleep_range(int lo,int hi) {now+=1000;}
static bool adreno_is_a619_holi(struct adreno_device *a) {return a->holi;}
static void adreno_read_gmu_wrapper(struct adreno_device *a,int reg,u32 *out) {++wrapper_reads;*out=cx_on?BIT(31):0;}
static void gmu_core_regread(struct kgsl_device *d,int reg,u32 *out) {++cx_reads;*out=cx_on?BIT(31):0;}
static bool adreno_has_sptprac_gdsc(struct adreno_device *a) {return a->sptprac;}
static bool adreno_has_gbif(struct adreno_device *a) {return a->gbif;}
static bool adreno_is_a660(struct adreno_device *a) {return a->a660;}
static bool a6xx_gmu_gx_is_on(struct kgsl_device *d) {return gx_on;}
static int timed_poll_check_rscc(struct kgsl_device *d,int reg,int val,int time,int mask) {++rpm_calls;return (int)rpm_calls==rpm_error_at?-EAGAIN:0;}
static int timed_poll_check(struct kgsl_device *d,int reg,int val,int time,int mask) {return spt_error;}
static void kgsl_regwrite(struct kgsl_device *d,int r,int v) {++writes;}
static void gmu_core_regwrite(struct kgsl_device *d,int r,int v) {++writes;}
static void gmu_core_regrmw(struct kgsl_device *d,int r,int mask,int v) {++writes;}
static int adreno_wait_for_halt_ack(struct kgsl_device *d,int reg,int mask) {++halt_calls;return (int)halt_calls==halt_error_at?-EBUSY:0;}
static void wmb(void) {}
static void udelay(int n) {}
static void ndelay(int n) {}
static void a6xx_gmu_irq_disable(struct adreno_device *a) {}
static void clk_bulk_disable_unprepare(int n,void *c) {++clk_off;}
static void a6xx_rdpm_cx_freq_update(struct a6xx_gmu_device *g,int f) {++rdpm;}
static int a6xx_gmu_domain_put(void *pd,bool *held) {assert(false);return -EIO;}
static int pm_runtime_resume_and_get(void *pd) {assert(false);return -EIO;}
static bool adreno_is_a680(struct adreno_device *a) {return false;}
int a6xx_cx_regulator_disable_wait(struct regulator *reg,struct kgsl_device *d,u32 timeout);
static int a6xx_gmu_disable_clks(struct adreno_device *a) {clk_bulk_disable_unprepare(0,NULL);return 0;}
static int a6xx_gmu_disable_cx(struct adreno_device *a) {return a6xx_cx_regulator_disable_wait(a->gmu.cx_gdsc,&a->device,5000);}
#include "kgsl-reset-functions.h"
static struct adreno_device reset(void) {
 cx_disable_error=gx_enable_error=gx_disable_error=mode_error[0]=mode_error[1]=spt_error=rpm_error_at=halt_error_at=0;
 rpm_calls=halt_calls=writes=clk_off=rdpm=enable_calls=mode_calls=wrapper_reads=cx_reads=0;
 for(int i=0;i<3;++i)disable_calls[i]=0;
 now=0;gx_on=true;cx_on=stuck_gx=false;
 return (struct adreno_device){.device={.state=7},.gmu={.cx_gdsc=&cx,.gx_gdsc=&gx},.gbif=true,.sptprac=true};
}
int main(void) {
 unsigned cases=0;
 for(int n=0;n<10;++n) {
  struct adreno_device a=reset();struct regulator *reg=&cx;int want=0;
  if(n==1){reg=NULL;want=-ENODEV;}if(n==2){reg=ERR_PTR(-EIO);want=-EIO;}
  if(n==3){cx_disable_error=-EACCES;want=-EACCES;}
  if(n==4){cx_on=true;want=-ETIMEDOUT;}
  if(n==5)a.holi=true;
  if(n>=6)a.quirk=true;
  if(n==7){mode_error[0]=-EBUSY;want=-EBUSY;}
  if(n==8){mode_error[1]=-EIO;want=-EIO;}
  if(n==9){cx_disable_error=-EACCES;mode_error[1]=-EIO;want=-EACCES;}
  assert(a6xx_cx_regulator_disable_wait(reg,&a.device,1)==want);
  if(n==3 || n==7 || n==9)assert(!cx_reads && !wrapper_reads);
  if(n==5)assert(wrapper_reads && !cx_reads);
  if(n==7)assert(!disable_calls[1] && mode_calls==1);
  if(n>=8)assert(mode_calls==2);
  ++cases;
 }
 for(int n=0;n<=4;++n) {struct adreno_device a=reset();rpm_error_at=n;assert(a6xx_complete_rpmh_votes(&a)==(n?-EAGAIN:0));assert(rpm_calls==(unsigned)(n?n:4));++cases;}
 for(int n=0;n<=2;++n) {struct adreno_device a=reset();halt_error_at=n;assert(a6xx_halt_gbif(&a)==(n?-EBUSY:0));assert(halt_calls==(unsigned)(n?n:2));++cases;}
 for(int n=0;n<13;++n) {
  struct adreno_device a=reset();int want=0;
  if(n==1){spt_error=-ETIMEDOUT;want=spt_error;}
  if(n==2){rpm_error_at=2;want=-EAGAIN;}
  if(n==3){halt_error_at=1;want=-EBUSY;}
  if(n==4){halt_error_at=2;want=-EBUSY;}
  if(n==5){halt_error_at=3;want=-EBUSY;}
  if(n==6){a.gmu.gx_gdsc=NULL;want=-ENODEV;}
  if(n==7){gx_enable_error=-EIO;want=-EIO;}
  if(n==8){gx_disable_error=-EACCES;want=-EACCES;}
  if(n==9){stuck_gx=true;want=-ETIMEDOUT;}
  if(n==10){cx_disable_error=-EIO;want=-EIO;}
  if(n==11)a.a660=true;
  if(n==12){gx_on=false;a.gmu.gx_gdsc=NULL;a.sptprac=false;}
  assert(a6xx_gmu_suspend(&a)==want);
  assert(a.gmu.power_error==want && a.device.state==(want?7:0));
  assert(clk_off==(n==10 || !want) && rdpm==!want);
  if(n>=1 && n<=5)assert(!enable_calls && !disable_calls[2]);
  if(n==7)assert(!disable_calls[2]);
  ++cases;
 }
 {struct adreno_device a=reset();a.gmu.power_error=-EIO;spt_error=-ETIMEDOUT;assert(a6xx_gmu_suspend(&a)==-ETIMEDOUT && a.gmu.power_error==-EIO);++cases;}
 {struct adreno_device a=reset();a.gmu.power_error=-EIO;assert(a6xx_gmu_enable_gdsc(&a)==-EIO && !enable_calls);++cases;}
 printf("%u actual reset/CX/SPTPRAC/RPMh/GBIF cases passed; MMIO/regulators/time mocked\n",cases);return 0;
}
