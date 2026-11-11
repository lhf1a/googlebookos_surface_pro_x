/* Real adreno functions are included by the checker; kernel leaves are models. */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sched.h>
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define KGSL_FLAG_WAKE_ON_TOUCH 1
#define EV_ABS 3
#define KGSL_STATE_NAP 1
#define KGSL_STATE_SLUMBER 2
#define GMU_DISPATCH 1
#define ADRENO_DEVICE_PWRON_FIXUP 2
#define ADRENO_DEVICE_INITIALIZED 3
struct work_struct { pthread_mutex_t lock; bool initialized,pending,running,joinable; pthread_t thread; void (*fn)(struct work_struct *); };
struct kgsl_device { pthread_mutex_t mutex; unsigned flags; int state,context_idr; void *pdev; struct {bool input_disable;} pwrctrl; struct {unsigned long flags;} gmu_core; void *dev; };
struct adreno_device { struct kgsl_device dev; struct work_struct input_work; void *gpu_llc_slice,*gpuhtw_llc_slice; unsigned long priv; };
struct device { struct kgsl_device *data; };
struct input_handler { void *private; };
struct input_handle { struct input_handler *handler; };
struct adreno_power_ops { void (*touch_wakeup)(struct adreno_device *); };
struct adreno_gpudev { void (*remove)(struct adreno_device *); };
static struct input_handler adreno_input_handler;
static struct adreno_device gpu,other;
static struct device outer;
static _Atomic int failures,freed,cancelled,unregistered,touched,worker_entered,producer_stop,producer_started,producer_events;
static bool start_worker,producer_active,send_final_event,require_cancel;
static pthread_t producer;
static void *adreno_ft_regs,*adreno_ft_regs_val,*efuse_base;
static void check(bool good,const char *why) { if (!good) { fprintf(stderr,"FAIL: %s\n",why); atomic_fetch_add(&failures,1); } }
static void mutex_lock(pthread_mutex_t *m) { atomic_store(&worker_entered,1); pthread_mutex_lock(m); }
static void mutex_unlock(pthread_mutex_t *m) { pthread_mutex_unlock(m); }
static void touch(struct adreno_device *d) { (void)d; check(!atomic_load(&freed),"worker accessed released resource"); atomic_fetch_add(&touched,1); }
static struct adreno_power_ops power_ops={touch};
#define KGSL_DEVICE(d) (&(d)->dev)
#define ADRENO_DEVICE(d) container_of(d,struct adreno_device,dev)
#define ADRENO_POWER_OPS(d) (&power_ops)
static struct kgsl_device *dev_get_drvdata(struct device *d) { return d->data; }
static void dev_set_drvdata(struct device *d,struct kgsl_device *data) {d->data=data;}
static void idr_destroy(int *idr) {(void)idr;}
static void kgsl_bus_close(struct kgsl_device *d) {(void)d;}
static int input_register_handler(struct input_handler *h) {(void)h;return -12;}
#define dev_err(...) ((void)0)
static bool gmu_core_isenabled(struct kgsl_device *d) { (void)d; return true; }
static void kgsl_start_idle_timer(struct kgsl_device *d) { (void)d; }
static void *run_work(void *p) { struct work_struct *w=p; w->fn(w); pthread_mutex_lock(&w->lock); w->running=false; pthread_mutex_unlock(&w->lock); return NULL; }
static void schedule_work(struct work_struct *w) {
 pthread_mutex_lock(&w->lock);
 check(w->initialized,"schedule uninitialized work");
 if (!w->running && !w->pending) { w->pending=true; if(start_worker) {w->pending=false;w->running=true;w->joinable=true;pthread_create(&w->thread,NULL,run_work,w);} }
 pthread_mutex_unlock(&w->lock);
}
static bool __attribute__((unused)) cancel_work_sync(struct work_struct *w) {
 atomic_fetch_add(&cancelled,1);
 check(w->initialized,"cancel uninitialized work");
#ifdef CONFIG_INPUT
 check(adreno_input_handler.private!=&gpu.dev,"cancel before producer unregister");
#endif
 int lockret=pthread_mutex_trylock(&gpu.dev.mutex);
 /* In blocked-worker cases the main thread owns the mutex, not unbind. */
 if (!lockret) pthread_mutex_unlock(&gpu.dev.mutex);
 pthread_mutex_lock(&w->lock); bool join=w->joinable; bool pending=w->pending;w->pending=false; pthread_mutex_unlock(&w->lock);
 if(join) {pthread_join(w->thread,NULL);w->joinable=false;}
 return pending;
}
static void adreno_input_event(struct input_handle *,unsigned int,unsigned int,int);
static void input_unregister_handler(struct input_handler *h) {
 check(h->private==&gpu.dev,"unregistered another device handler");
 if(send_final_event) {struct input_handle handle={h};adreno_input_event(&handle,EV_ABS,0,1);}
 if(producer_active) {atomic_store(&producer_stop,1);pthread_join(producer,NULL);producer_active=false;}
 atomic_fetch_add(&unregistered,1);
}
static void resource_free(void) {
#ifdef CONFIG_INPUT
 check(adreno_input_handler.private!=&gpu.dev,"resource release before producer stopped");
#endif
 pthread_mutex_lock(&gpu.input_work.lock);
 check(!gpu.input_work.running && !gpu.input_work.pending,"resource release before work drained");
 pthread_mutex_unlock(&gpu.input_work.lock);
 check(!require_cancel || atomic_load(&cancelled)>0,"no synchronous input work drain");
 check(!producer_active,"resource release with concurrent producer");
 atomic_store(&freed,1);
}
static void remove_gpu(struct adreno_device *d) {(void)d;resource_free();}
static struct adreno_gpudev gpu_ops={remove_gpu};
#define ADRENO_GPU_DEVICE(d) (&gpu_ops)
static void _adreno_free_memories(struct adreno_device *d) {(void)d;resource_free();}
#define STUB_ADRENO(n) static void n(struct adreno_device *d) {(void)d;}
STUB_ADRENO(adreno_sysfs_close) STUB_ADRENO(adreno_coresight_remove)
STUB_ADRENO(adreno_profile_close) STUB_ADRENO(adreno_dispatcher_close)
STUB_ADRENO(adreno_ringbuffer_close) STUB_ADRENO(adreno_fault_detect_stop)
static void kgsl_pwrscale_close(struct kgsl_device *d) {(void)d;}
static void kgsl_device_platform_remove(struct kgsl_device *d) {(void)d;}
static void adreno_unbind_gmu(struct kgsl_device *d) {(void)d;}
#define IS_ERR_OR_NULL(p) (!(p))
static void llcc_slice_putd(void *p) {(void)p;}
static void iounmap(void *p) {(void)p;}
#define kfree free
static bool test_bit(unsigned n,unsigned long *p) {return !!(*p&(1UL<<n));}
static void clear_bit(unsigned n,unsigned long *p) {*p &= ~(1UL<<n);}
/* GENERATED_FUNCTIONS */
static void *produce(void *p) {
 /* Model a callback that already read private before unregister begins. */
 struct input_handler snapshot=*(struct input_handler *)p;
 struct input_handle h={&snapshot};
 while(!atomic_load(&producer_stop)) {adreno_input_event(&h,EV_ABS,0,1);atomic_fetch_add(&producer_events,1);atomic_store(&producer_started,1);sched_yield();}
 return NULL;
}
static void *unbind_thread(void *p) {adreno_unbind(p);return NULL;}
int main(int argc,char **argv) {
 if(argc!=2)return 2;
 const char *s=argv[1];
 memset(&gpu,0,sizeof(gpu));pthread_mutex_init(&gpu.dev.mutex,NULL);pthread_mutex_init(&gpu.input_work.lock,NULL);
 gpu.input_work.initialized=true;gpu.input_work.fn=adreno_input_work;gpu.dev.state=KGSL_STATE_SLUMBER;outer.data=&gpu.dev;require_cancel=true;
#ifdef CONFIG_INPUT
 adreno_input_handler.private=&gpu.dev;
#endif
 struct input_handle h={&adreno_input_handler};
 bool running=!strcmp(s,"running") || !strcmp(s,"running-no-remove");
 if(!strcmp(s,"null-drvdata")) {outer.data=NULL;gpu.input_work.initialized=false;require_cancel=false;}
 else if(!strcmp(s,"probe-error")) {check(probe_error_cleanup(&outer,&gpu.dev,-5)==-5,"probe cleanup lost errno");require_cancel=false;}
 else if(!strcmp(s,"register-failed"))probe_input_register(&gpu.dev);
 else if(!strcmp(s,"input-disabled")) {adreno_input_handler.private=NULL;gpu.dev.pwrctrl.input_disable=true;probe_input_register(&gpu.dev);}
 else if(!strcmp(s,"other-owner"))adreno_input_handler.private=&other.dev;
 else if(!strcmp(s,"pending")) schedule_work(&gpu.input_work);
 else if(!strcmp(s,"last-event"))send_final_event=true;
 else if(!strcmp(s,"producer")) {producer_active=true;pthread_create(&producer,NULL,produce,&adreno_input_handler);while(!atomic_load(&producer_started))sched_yield();}
 else if(!strcmp(s,"nonabs"))adreno_input_event(&h,0,0,1);
 else if(!strcmp(s,"wake-flag")) {gpu.dev.flags=KGSL_FLAG_WAKE_ON_TOUCH;adreno_input_event(&h,EV_ABS,0,1);}
 else if(!strcmp(s,"no-remove"))gpu_ops.remove=NULL;
 else if(!strcmp(s,"repeat")){}
 else if(!running && strcmp(s,"idle")) return 2;
 if(running) {
  if(!strcmp(s,"running-no-remove"))gpu_ops.remove=NULL;
  start_worker=true;pthread_mutex_lock(&gpu.dev.mutex);schedule_work(&gpu.input_work);
  while(!atomic_load(&worker_entered))sched_yield();
  pthread_t unbinder;pthread_create(&unbinder,NULL,unbind_thread,&outer);
  while(!atomic_load(&cancelled) && !atomic_load(&freed))sched_yield();
  pthread_mutex_unlock(&gpu.dev.mutex);pthread_join(unbinder,NULL);
 } else adreno_unbind(&outer);
 if(gpu.input_work.joinable){pthread_join(gpu.input_work.thread,NULL);gpu.input_work.joinable=false;}
 if(producer_active){atomic_store(&producer_stop,1);pthread_join(producer,NULL);producer_active=false;}
 if(!strcmp(s,"repeat")) {adreno_unbind(&outer);check(atomic_load(&unregistered)<=1,"repeat unregister");}
 if(!outer.data)check(!atomic_load(&cancelled),"cancelled work on missing/failed device");
#ifdef CONFIG_INPUT
 if(!strcmp(s,"other-owner"))check(adreno_input_handler.private==&other.dev,"changed another device owner");
 else if(outer.data)check(adreno_input_handler.private==NULL,"stale handler private after unregister");
#endif
 if(running)check(atomic_load(&touched)==1,"running worker was not joined");
 printf("%s: failures=%d unregister=%d cancel=%d touch=%d producer_events=%d\n",s,atomic_load(&failures),atomic_load(&unregistered),atomic_load(&cancelled),atomic_load(&touched),atomic_load(&producer_events)>0);
 return atomic_load(&failures)?1:0;
}
