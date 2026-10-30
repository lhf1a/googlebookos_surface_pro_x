/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#define module_param(a,b,c)
static void mock_log(const char *fmt,...) {(void)fmt;}
#define dev_err(d,...) ((void)(d),mock_log(__VA_ARGS__))
#define dev_warn(d,...) ((void)(d),mock_log(__VA_ARGS__))
#define CORE_UNINIT 0
#define HFI_VERSION_4XX 4
#define VIDC_SESSION_TYPE_DEC 2
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
typedef int refcount_t;
#define SQ2_VENUS_RETIRE_HOST_TEST
struct list_head { struct list_head *next, *prev; };
struct work_struct { int unused; };
struct delayed_work { struct work_struct work; };
struct device { void *of_node; };
struct platform_device {struct device dev;void *data;};
struct venus_resources {bool iris1;int hfi_version;};
struct venus_pm_ops {int unused;};
struct venus_core;
struct ops {int (*core_deinit)(struct venus_core *);};
struct sq2_venus_platform;
struct venus_core {
 struct device *dev; const struct venus_resources *res;const struct venus_pm_ops *pm_ops;
 struct sq2_venus_platform *sq2_platform;struct ops *ops;void *priv;
 bool use_tz,sq2_public,sq2_children,sq2_pm_enabled;
 int lock,pm_lock,instances,sys_err_done,state,v4l2_dev;
 unsigned long sys_error;struct delayed_work work;
};
struct sq2_venus_platform {struct venus_core core;};
static struct sq2_venus_platform owner;
static int step,fail,allocated,power,hfi,irq,children,pm,boots,stop_error,stops,cancelled;
static int retired_drained;
void sq2_venus_retire_init(struct venus_core *c) {(void)c; retired_drained=0;}
void sq2_venus_retire_stopped(struct venus_core *c)
{(void)c;assert(!irq && !stop_error);retired_drained++;}
static const int venus_core_ops=0;
static int bad(void){return ++step==fail;}
static void mutex_lock(int *p){(void)p;}static void mutex_unlock(int *p){(void)p;}
static void mutex_init(int *p){*p=1;}static void mutex_destroy(int *p){*p=0;}
static void set_bit(int b,unsigned long *p){*p|=1UL<<b;}
#define INIT_LIST_HEAD(p) (*(p)=0)
#define INIT_DELAYED_WORK(p,f) ((void)(p),(void)(f))
static void init_waitqueue_head(int *p){*p=0;}
static void cancel_delayed_work_sync(struct delayed_work *w){(void)w;cancelled++;}
static void *platform_get_drvdata(struct platform_device *p){return p->data;}
static void platform_set_drvdata(struct platform_device *p,void *c){p->data=c;}
int sq2_venus_platform_alloc(struct platform_device *p,struct sq2_venus_platform **o)
{if(bad())return -ENOMEM;allocated=1;owner.core.dev=&p->dev;owner.core.sq2_platform=&owner;*o=&owner;return 0;}
struct venus_core *sq2_venus_platform_core(struct sq2_venus_platform *o){return &o->core;}
int sq2_venus_platform_irq(struct sq2_venus_platform *o,bool enable)
{(void)o;if(enable&&bad())return -EIO;irq=enable;return 0;}
int sq2_venus_platform_power_on(struct sq2_venus_platform *o){(void)o;power=1;return bad()?-EIO:0;}
int sq2_venus_platform_power_off(struct sq2_venus_platform *o){(void)o;assert(!irq);power=0;return 0;}
int sq2_venus_platform_release(struct sq2_venus_platform **o){assert(!power&&!hfi&&!irq);allocated=0;*o=NULL;return 0;}
static int deinit(struct venus_core *c){(void)c;return 0;}static struct ops ops={deinit};
static void venus_assign_register_offsets(struct venus_core *c){(void)c;}
static int hfi_create(struct venus_core *c,const void *o)
{(void)o;if(bad())return -EIO;hfi=1;c->priv=&owner;c->ops=&ops;return 0;}
static void hfi_destroy(struct venus_core *c){assert(!power);hfi=0;c->priv=NULL;}
static int venus_boot(struct venus_core *c){(void)c;boots++;return bad()?-EIO:0;}
static int venus_shutdown(struct venus_core *c){(void)c;stops++;return boots?stop_error:0;}
static int hfi_core_resume(struct venus_core *c,bool first){(void)c;assert(first);return bad()?-EIO:0;}
static int hfi_core_init(struct venus_core *c){(void)c;assert(irq);return bad()?-EIO:0;}
static int venus_enumerate_codecs(struct venus_core *c,int t){(void)c;assert(t==2);return bad()?-EIO:0;}
static int v4l2_device_register(struct device *d,int *v){(void)d;*v=1;return bad()?-EIO:0;}
static int pm_runtime_set_active(struct device *d){(void)d;return bad()?-EIO:0;}
static void pm_runtime_enable(struct device *d){(void)d;pm=1;}
static void pm_runtime_disable(struct device *d){(void)d;pm=0;}
static int of_platform_populate(void *n,void *a,void *b,struct device *d)
{(void)n;(void)a;(void)b;(void)d;children=1;return bad()?-EIO:0;}
static void of_platform_depopulate(struct device *d){(void)d;children=0;}
#ifndef CORE_ENTRY_HEADER
#define CORE_ENTRY_HEADER "../probe/video/venus-sq2-core.h"
#endif
#include CORE_ENTRY_HEADER
static void reset(void)
{memset(&owner,0,sizeof(owner));step=allocated=power=hfi=irq=children=pm=boots=stops=cancelled=0;stop_error=0;}
int main(int argc,char **argv)
{
 struct platform_device p={0};const struct venus_resources r={true,4};int f,ret;
 (void)argv;sq2_platform_allow=true;
 if(argc>1) {
  reset();fail=11;ret=venus_sq2_probe(&p,&r);
  printf("populate_failure ret=%d pm_enabled=%d children=%d retained=%d\n",ret,pm,children,allocated);
  return 0;
 }
 for(f=1;f<=11;f++) {
  reset();p.data=NULL;fail=f;ret=venus_sq2_probe(&p,&r);
  assert(ret<0&&!children&&!pm&&!irq);
  if(f<10) assert(!allocated&&!hfi&&!power);
  else assert(allocated&&hfi&&!power);
 }
 reset();p.data=NULL;fail=5;stop_error=-ETIMEDOUT;
 assert(venus_sq2_probe(&p,&r)==-EIO);
 assert(allocated&&power&&hfi&&!irq&&boots==1);
 assert(!retired_drained);
 reset();p.data=NULL;fail=0;
 assert(!venus_sq2_probe(&p,&r)&&children&&pm&&irq);
 venus_sq2_error(&owner.core.work.work);
 assert(boots==1&&!power&&!irq&&allocated&&hfi);
 assert(retired_drained==1);
 assert(!venus_sq2_remove(&p)&&!children&&!pm&&allocated&&hfi);
 reset();p.data=NULL;fail=0;sq2_platform_allow=false;
 assert(venus_sq2_probe(&p,&r)==-EOPNOTSUPP&&!allocated&&step==0);
 puts("SQ2_CORE_ENTRY_PASS: actual entry C, 11 probe fault positions, PAS failure retention, terminal error without restart, remove retention and opt-in guard; mock providers, not concurrency proof");
 return 0;
}
