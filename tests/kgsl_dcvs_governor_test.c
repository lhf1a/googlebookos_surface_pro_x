#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
typedef uint32_t u32;typedef int32_t s32;typedef uint64_t u64;typedef int64_t s64;
#define U32_MAX UINT32_MAX
#define MAX_TZ_VERSION 0
#define FLOOR 5000
#define MIN_BUSY 1000
#define CEILING 50000
#define TAG ""
#define pr_err(...) ((void)0)
#define do_div(n,d) ((n)/=(d))
#define min_t(t,a,b) ((t)(a)<(t)(b)?(t)(a):(t)(b))
#define clamp_t(t,v,lo,hi) ((t)(v)<(t)(lo)?(t)(lo):(t)(v)>(t)(hi)?(t)(hi):(t)(v))
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define MSM_ADRENO_MAX_PWRLEVELS 16
#define INIT_WORK(w,f) ((void)(w))
struct notifier_block {int (*notifier_call)(void);};
struct device {struct device *parent;};
struct device_attribute {int id;};
struct devfreq_msm_adreno_tz_data {
 struct notifier_block nb;
 struct {s64 total_time,busy_time;u32 ctxt_aware_target_pwrlevel,ctxt_aware_busy_penalty;} bin;
 bool is_64,ctxt_aware_enable,disable_busy_time_burst;u32 mod_percent;
};
struct devfreq_dev_status {unsigned long total_time,busy_time,current_frequency;void *private_data;};
struct devfreq_dev_profile {unsigned int max_state;unsigned long *freq_table;};
struct msm_adreno_extended_profile {
 struct devfreq_msm_adreno_tz_data *private_data;
 int partner_start_event_ws,partner_stop_event_ws,partner_suspend_event_ws,partner_resume_event_ws;
 struct devfreq_dev_profile profile;
};
struct devfreq {void *data;struct devfreq_dev_profile *profile;struct devfreq_dev_status last_status;struct device dev;};
static struct msm_adreno_extended_profile *partner_gpu_profile;
static struct device_attribute attrs[3];
static const struct device_attribute *adreno_tz_attr_list[]={&attrs[0],&attrs[1],&attrs[2],NULL};
static int init_error,ca_error,update_error,reset_error,status_error,file_error_at,notifier_error;
static unsigned init_calls,ca_calls,update_calls,reset_calls,created,removed,notifiers;
static u32 version_response;static s32 delta_response;
static unsigned long table[]={600000000,400000000,200000000};
static int qcom_scm_gpu_dcvs_init(const u32 *levels,size_t count,u32 *version) {++init_calls;assert(count==4 && levels[0]==3 && levels[1]==table[0] && levels[2]==table[1] && levels[3]==table[2]);if(!init_error)*version=version_response;return init_error;}
static int qcom_scm_gpu_dcvs_init_ca(u32 level,u32 penalty) {++ca_calls;return ca_error;}
static int qcom_scm_gpu_dcvs_reset(void) {++reset_calls;return reset_error;}
static int qcom_scm_gpu_dcvs_update(u32 level,u64 total,u64 busy,bool ca,u32 contexts,s32 *delta) {++update_calls;assert(level==1 && total>=6000 && busy>=2000);if(!update_error)*delta=delta_response;return update_error;}
static int devfreq_update_stats(struct devfreq *d) {return status_error;}
static void compute_work_load(struct devfreq_dev_status *s,struct devfreq_msm_adreno_tz_data *p,struct devfreq *d) {}
static int tz_notify(void) {return 0;}
static int device_create_file(struct device *d,const struct device_attribute *attr) {if((int)created==file_error_at)return -ENOMEM;++created;return 0;}
static void device_remove_file(struct device *d,const struct device_attribute *attr) {++removed;}
static int kgsl_devfreq_add_notifier(struct device *d,struct notifier_block *nb) {++notifiers;return notifier_error;}
#include "governor-functions.h"
static void reset(void) {init_error=ca_error=update_error=reset_error=status_error=notifier_error=0;file_error_at=-1;init_calls=ca_calls=update_calls=reset_calls=created=removed=notifiers=0;version_response=0;delta_response=0;partner_gpu_profile=NULL;}
static struct devfreq_msm_adreno_tz_data priv;
static struct msm_adreno_extended_profile profile;
static struct devfreq devfreq;
static void fixture(void) {reset();priv=(struct devfreq_msm_adreno_tz_data){.is_64=true,.mod_percent=100};profile=(struct msm_adreno_extended_profile){.private_data=&priv,.profile={3,table}};devfreq=(struct devfreq){.data=&priv,.profile=&profile.profile,.last_status={6000,2000,400000000,NULL}};}
int main(void) {
 unsigned cases=0;u32 levels[]={3,600000000,400000000,200000000},version;
 for(int scenario=0;scenario<5;++scenario) {fixture();version=99;
  if(scenario==1)init_error=-EBUSY;
  if(scenario==2)version_response=1;
  if(scenario>=3)priv.ctxt_aware_enable=true;
  if(scenario==4)ca_error=-EIO;
  int want=scenario==1?-EBUSY:scenario==2?-EPROTO:scenario==4?-EIO:0;
  assert(tz_init(&priv,levels,4,&version)==want && priv.is_64==!want);
  assert(reset_calls==(scenario==2 || scenario==4));assert(priv.ctxt_aware_enable==(scenario>=3));++cases;
 }
 for(int scenario=0;scenario<8;++scenario) {fixture();unsigned long freq=0;
  if(scenario==0)update_error=-EIO;
  if(scenario==1)update_error=-EBUSY;
  if(scenario==2)delta_response=-1;
  if(scenario==3)delta_response=1;
  if(scenario==4)delta_response=INT_MAX;
  if(scenario==5)delta_response=INT_MIN;
  if(scenario==6)status_error=-EFAULT;
  if(scenario==7)priv.is_64=false;
  int want=scenario<2?update_error:scenario==6?-EFAULT:scenario==7?-EOPNOTSUPP:0;
  assert(tz_get_target_freq(&devfreq,&freq)==want);
  if(scenario<2 || scenario==7)assert(freq==400000000 && priv.bin.total_time==6000 && priv.bin.busy_time==2000);
  if(scenario>=2 && scenario<=5)assert(freq==((scenario==2 || scenario==5)?600000000:200000000) && !priv.bin.total_time && !priv.bin.busy_time);
  if(scenario==6 || scenario==7)assert(!update_calls);
  ++cases;
 }
 for(int scenario=0;scenario<3;++scenario) {fixture();priv.bin.total_time=6000;priv.bin.busy_time=2000;
  if(scenario==1)reset_error=-ETIMEDOUT;
  if(scenario==2)priv.is_64=false;
  assert(tz_suspend(&devfreq)==(scenario==2?-EOPNOTSUPP:reset_error));
  assert(priv.bin.total_time==(scenario?6000:0) && priv.bin.busy_time==(scenario?2000:0));++cases;
 }
 for(int scenario=0;scenario<9;++scenario) {fixture();int want=0;
  if(scenario==1){init_error=-EIO;want=-EIO;}
  if(scenario==2){version_response=1;want=-EPROTO;}
  if(scenario>=3 && scenario<=5){file_error_at=scenario-3;want=-ENOMEM;}
  if(scenario==6){notifier_error=-EAGAIN;want=-EAGAIN;}
  if(scenario==7){profile.profile.max_state=0;want=-EINVAL;}
  if(scenario==8){table[1]=1ULL<<32;want=-ERANGE;}
  assert(tz_start(&devfreq)==want);
  assert((partner_gpu_profile==&profile)==!want);
  if(scenario>=3 && scenario<=6)assert(removed==created && reset_calls==1);
  if(!want)assert(created==3 && notifiers==1 && !removed);
  if(scenario==7 || scenario==8)assert(!init_calls && !notifiers && !created);
  table[1]=400000000;++cases;
 }
 printf("%u governor scenarios passed (SCM, devfreq and resource operations mocked)\n",cases);return 0;
}
