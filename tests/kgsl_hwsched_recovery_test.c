/* Actual recovery functions with controlled hardware and ownership models. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
typedef uint32_t u32;
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
struct list_head { struct list_head *next,*prev; };
static void INIT_LIST_HEAD(struct list_head *h) { h->next=h->prev=h; }
static bool list_empty(const struct list_head *h) { return h->next==h; }
static void list_add_tail(struct list_head *n,struct list_head *h) { n->prev=h->prev; n->next=h; h->prev->next=n; h->prev=n; }
static void list_del_init(struct list_head *n) { n->prev->next=n->next; n->next->prev=n->prev; INIT_LIST_HEAD(n); }
#define list_for_each_entry(p,h,m) for(p=container_of((h)->next,__typeof__(*p),m); &p->m!=(h);p=container_of(p->m.next,__typeof__(*p),m))
#define list_for_each_entry_safe(p,t,h,m) for(p=container_of((h)->next,__typeof__(*p),m),t=container_of(p->m.next,__typeof__(*p),m); &p->m!=(h);p=t,t=container_of(t->m.next,__typeof__(*t),m))
static bool test_bit(int b,unsigned long *p) { return *p&(1UL<<b); }
static void set_bit(int b,unsigned long *p) { *p|=1UL<<b; }
static void clear_bit(int b,unsigned long *p) { *p &= ~(1UL<<b); }
static bool test_and_clear_bit(int b,unsigned long *p) { bool v=test_bit(b,p); clear_bit(b,p); return v; }
static bool kref_get_unless_zero(int *p) { if(!*p)return false; (*p)++;return true; }
static void mutex_lock(int *p) { assert(!*p);*p=1; }
static void mutex_unlock(int *p) { assert(*p);*p=0; }
#define read_lock mutex_lock
#define read_unlock mutex_unlock
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define smp_wmb() ((void)0)
static int atomic_read(int *p) { return *p; }
static void atomic_set(int *p,int v) { *p=v; }
static int atomic_cmpxchg(int *p,int old,int new) { int v=*p; if(v==old)*p=new;return v; }
enum { ADRENO_HWSCHED_POWER,ADRENO_HWSCHED_FAULT_RESTART,ADRENO_HWSCHED_FAULT_REPLAY,ADRENO_HWSCHED_FAULT_RESETTING };
enum { CMDOBJ_FAULT,KGSL_FT_REPLAY,GMU_PRIV_GPU_STARTED };
#define KGSL_STATE_ACTIVE 1
#define KGSL_CONTEXT_INVALIDATE_ON_FAULT 1
struct kgsl_device { int mutex,state,context_lock,context_idr; struct { int transition_error; } pwrctrl; };
struct kgsl_event_group { int cancelled; };
struct kgsl_context { int id; unsigned flags; bool invalid,detached; u32 retired; struct kgsl_event_group events; };
struct kgsl_drawobj { struct kgsl_context *context; u32 timestamp; int refcount; bool active; int destroyed; };
struct kgsl_mem_entry { int memdesc; };
struct kgsl_drawobj_profiling_buffer { int x; };
struct kgsl_drawobj_cmd { struct kgsl_drawobj base; unsigned long priv,fault_policy; struct kgsl_mem_entry *profiling_buf_entry; unsigned long profiling_buffer_gpuaddr; };
struct cmd_list_obj { struct kgsl_drawobj_cmd *cmdobj; struct list_head node; };
struct adreno_hwsched { int mutex,fault; unsigned inflight; struct list_head cmd_list; };
struct a6xx_gmu_device { unsigned long flags; int power_error; };
struct adreno_device { struct kgsl_device dev; struct adreno_hwsched scheduler; struct a6xx_gmu_device gmu; int halt; };
#define DRAWOBJ(c) (&(c)->base)
#define KGSL_DEVICE(a) (&(a)->dev)
static struct adreno_hwsched *to_hwsched(struct adreno_device *a) { return &a->scheduler; }
static struct a6xx_gmu_device *to_a6xx_gmu(struct adreno_device *a) { return &a->gmu; }
static struct adreno_device ad;
static struct kgsl_context contexts[3];
static struct kgsl_drawobj_cmd commands[4];
static int suspend_result,boot_result,submit_error,submit_fail_at,new_fault_boot,new_fault_submit,new_fault_events;
static int suspend_calls,boot_calls,submit_calls,stops,snapshots,headers,invalidations,cancels,frees,triggers,drains,clear_halts,profile_maps,profile_unmaps;
static bool reset_done,profile_fail;
static void *obj_cache;
static void adreno_hwsched_trigger(struct adreno_device *a) { triggers++; }
static void adreno_clear_gpu_halt(struct adreno_device *a) { a->halt=0;clear_halts++; }
static void a6xx_hwsched_hfi_stop(struct adreno_device *a) { stops++; }
static void a6xx_disable_gpu_irq(struct adreno_device *a) { }
static int a6xx_gmu_suspend(struct adreno_device *a) {
 suspend_calls++;
 if(suspend_result) return suspend_result;
 reset_done=true;
 for(unsigned i=0;i<4;i++) commands[i].base.active=false;
 a->dev.state=0;return 0;
}
static void a6xx_hwsched_drain_ctxt_unregister(struct adreno_device *a) { assert(reset_done);drains++; }
static int unregister_context_hwsched(int id,void *p,void *v) { return 0; }
static void idr_for_each(int *p,int (*fn)(int,void *,void *),void *v) { assert(reset_done); }
static int a6xx_hwsched_boot(struct adreno_device *a) {
 boot_calls++; assert(reset_done && !test_bit(GMU_PRIV_GPU_STARTED,&a->gmu.flags));
 if(new_fault_boot) atomic_set(&a->scheduler.fault,ADRENO_HWSCHED_FAULT_RESTART);
 if(!boot_result) { a->dev.state=KGSL_STATE_ACTIVE;set_bit(GMU_PRIV_GPU_STARTED,&a->gmu.flags); }
 return boot_result;
}
static void *kgsl_gpuaddr_to_vaddr(int *m,unsigned long addr) { profile_maps++;return profile_fail?NULL:m; }
static void kgsl_memdesc_unmap(int *m) { profile_unmaps++; }
static void kgsl_drawobj_put(struct kgsl_drawobj *d) { assert(d->refcount>0); d->refcount--; }
static void kgsl_drawobj_destroy(struct kgsl_drawobj *d) { assert(!d->active && !d->destroyed); d->destroyed++;kgsl_drawobj_put(d); }
static bool kgsl_context_invalid(struct kgsl_context *c) { return c->invalid; }
static bool kgsl_context_detached(struct kgsl_context *c) { return c->detached; }
static void adreno_mark_guilty_context(struct kgsl_device *d,int id) { assert(reset_done); }
static void adreno_drawctxt_invalidate(struct kgsl_device *d,struct kgsl_context *c) {
 assert(reset_done && submit_calls==0);c->invalid=true;invalidations++;
}
static bool kgsl_check_timestamp(struct kgsl_device *d,struct kgsl_context *c,u32 ts) { return (int32_t)(c->retired-ts)>=0; }
static void kgsl_cancel_events_timestamp(struct kgsl_device *d,struct kgsl_event_group *e,u32 ts) { assert(reset_done);e->cancelled++;cancels++; }
static void kmem_cache_free(void *cache,void *p) { struct cmd_list_obj *o=p;assert(o->cmdobj->base.destroyed);frees++;free(p); }
static int a6xx_hwsched_submit_cmdobj(struct adreno_device *a,struct kgsl_drawobj_cmd *c) {
 submit_calls++;assert(reset_done && !c->base.active && !c->base.context->invalid && !c->base.context->detached);
 assert(!test_bit(KGSL_FT_REPLAY,&c->fault_policy));
 if(submit_calls==submit_fail_at) return submit_error;
 c->base.active=true;
 if(new_fault_submit) atomic_set(&a->scheduler.fault,ADRENO_HWSCHED_FAULT_RESTART);
 return 0;
}
static void kgsl_process_event_groups(struct kgsl_device *d) { if(new_fault_events) atomic_set(&ad.scheduler.fault,ADRENO_HWSCHED_FAULT_RESTART); }
static void do_fault_header(struct adreno_device *a,struct kgsl_drawobj *d) { headers++; }
static void kgsl_device_snapshot(struct kgsl_device *d,struct kgsl_context *c,bool f) { snapshots++; }
#include "hwsched-recovery-functions.h"
static void setup(unsigned count) {
 memset(&ad,0,sizeof(ad));memset(contexts,0,sizeof(contexts));memset(commands,0,sizeof(commands));
 INIT_LIST_HEAD(&ad.scheduler.cmd_list);ad.scheduler.fault=ADRENO_HWSCHED_FAULT_RESTART;ad.scheduler.inflight=count;
 ad.dev.state=KGSL_STATE_ACTIVE;ad.halt=1;set_bit(GMU_PRIV_GPU_STARTED,&ad.gmu.flags);
 suspend_result=boot_result=submit_error=submit_fail_at=new_fault_boot=new_fault_submit=new_fault_events=0;
 suspend_calls=boot_calls=submit_calls=stops=snapshots=headers=invalidations=cancels=frees=triggers=drains=clear_halts=profile_maps=profile_unmaps=0;
 reset_done=profile_fail=false;
 for(unsigned i=0;i<3;i++)contexts[i].id=i+1;
 for(unsigned i=0;i<count;i++) {
  struct kgsl_drawobj_cmd *c=&commands[i];c->base.context=&contexts[i%3];c->base.timestamp=10+i;c->base.refcount=1;c->base.active=true;
  struct cmd_list_obj *o=calloc(1,sizeof(*o));assert(o);o->cmdobj=c;list_add_tail(&o->node,&ad.scheduler.cmd_list);
 }
}
static void fault(unsigned i) { adreno_hwsched_mark_drawobj(&ad,commands[i].base.context->id,commands[i].base.timestamp); }
static void complete(unsigned i) { commands[i].base.context->retired=commands[i].base.timestamp;commands[i].base.active=false; }
static void recover(void) { mutex_lock(&ad.scheduler.mutex);adreno_hwsched_recovery(&ad);mutex_unlock(&ad.scheduler.mutex); }
static void retained(unsigned i) { assert(!commands[i].base.destroyed && commands[i].base.refcount>=1); }
static void dead(unsigned i) { assert(commands[i].base.destroyed==1 && commands[i].base.refcount==0); }
static unsigned cases;
/* Cleanup only the modeled fixture. Production must retain failed owners. */
static void finish(void) {
 struct cmd_list_obj *p,*n;
 list_for_each_entry_safe(p,n,&ad.scheduler.cmd_list,node) { list_del_init(&p->node);free(p); }
 assert(ad.dev.mutex==0 && ad.scheduler.mutex==0);cases++;
}
int main(void) {
 setup(1);fault(0);fault(0);assert(commands[0].base.refcount==2);recover();
 dead(0);assert(suspend_calls==1 && boot_calls==1 && cancels==1 && !submit_calls && !ad.scheduler.inflight && !ad.scheduler.fault && clear_halts==1);assert(contexts[0].retired==0);finish();

 setup(1);fault(0);suspend_result=-ETIMEDOUT;recover();retained(0);
 assert(commands[0].base.refcount==2 && !boot_calls && !cancels && !invalidations && ad.scheduler.inflight==1 && ad.gmu.power_error==-ETIMEDOUT);recover();assert(suspend_calls==1);finish();

 setup(1);fault(0);boot_result=-EIO;recover();retained(0);
 assert(commands[0].base.refcount==2 && !cancels && ad.scheduler.inflight==1 && ad.dev.pwrctrl.transition_error==-EIO);finish();

 setup(1);fault(0);ad.dev.state=0;recover();retained(0);assert(!snapshots && !suspend_calls && ad.gmu.power_error==-EIO);finish();

 setup(1);clear_bit(GMU_PRIV_GPU_STARTED,&ad.gmu.flags);recover();retained(0);assert(!suspend_calls && ad.gmu.power_error==-EIO);finish();

 setup(1);ad.gmu.power_error=-EPERM;recover();retained(0);assert(!suspend_calls && !submit_calls);finish();

 setup(1);ad.dev.pwrctrl.transition_error=-EUCLEAN;recover();retained(0);assert(!suspend_calls && !submit_calls);finish();

 setup(1);fault(0);new_fault_boot=1;recover();retained(0);
 assert(ad.scheduler.fault==ADRENO_HWSCHED_FAULT_RESTART && !cancels && !submit_calls && ad.gmu.power_error==-EIO);finish();

 setup(2);recover();assert(submit_calls==2 && ad.scheduler.inflight==2 && ad.scheduler.fault==ADRENO_HWSCHED_FAULT_REPLAY);
 recover();assert(submit_calls==2 && ad.scheduler.inflight==2);complete(0);recover();dead(0);retained(1);assert(ad.scheduler.inflight==1);
 complete(1);recover();dead(1);assert(!ad.scheduler.inflight && !ad.scheduler.fault && clear_halts==1 && !cancels);finish();

 setup(2);recover();contexts[0].detached=true;recover();retained(0);assert(commands[0].base.active && ad.scheduler.inflight==2);
 complete(0);recover();dead(0);assert(ad.scheduler.inflight==1);finish();

 setup(1);recover();contexts[0].invalid=true;recover();retained(0);assert(ad.scheduler.inflight==1 && !cancels);finish();

 setup(2);contexts[0].detached=true;recover();dead(0);retained(1);assert(cancels==1 && submit_calls==1 && ad.scheduler.inflight==1);finish();

 setup(2);contexts[0].invalid=true;recover();dead(0);retained(1);assert(cancels==1 && submit_calls==1);finish();

 /* A fault later in the same context is invalidated before earlier replay. */
 setup(3);commands[1].base.context=&contexts[0];contexts[0].flags=KGSL_CONTEXT_INVALIDATE_ON_FAULT;fault(1);recover();
 dead(0);dead(1);retained(2);assert(invalidations==1 && submit_calls==1 && cancels==2 && ad.scheduler.inflight==1);finish();

 setup(3);fault(0);fault(1);recover();dead(0);dead(1);retained(2);assert(cancels==2 && submit_calls==1 && !test_bit(CMDOBJ_FAULT,&commands[0].priv));finish();

 setup(2);submit_fail_at=1;submit_error=-ENOMEM;recover();retained(0);retained(1);
 assert(!ad.scheduler.inflight && !test_bit(KGSL_FT_REPLAY,&commands[0].fault_policy) && ad.gmu.power_error==-ENOMEM && !clear_halts);
 recover();assert(submit_calls==1);finish();

 setup(2);submit_fail_at=2;submit_error=-EIO;recover();retained(0);retained(1);
 assert(ad.scheduler.inflight==1 && commands[0].base.active && !commands[1].base.active && ad.dev.pwrctrl.transition_error==-EIO);
 recover();assert(submit_calls==2 && !cancels);finish();

 setup(2);submit_fail_at=2;submit_error=-ENOSPC;recover();retained(0);retained(1);
 assert(ad.scheduler.inflight==1 && !ad.gmu.power_error && !test_bit(KGSL_FT_REPLAY,&commands[1].fault_policy));
 complete(0);recover();dead(0);assert(submit_calls==3 && ad.scheduler.inflight==1);
 complete(1);recover();dead(1);assert(!ad.scheduler.fault && !ad.scheduler.inflight);finish();

 setup(1);submit_fail_at=1;submit_error=-ENOSPC;recover();retained(0);
 assert(!ad.scheduler.inflight && ad.gmu.power_error==-ENOSPC && !clear_halts);finish();

 setup(2);new_fault_submit=1;recover();retained(0);retained(1);
 assert(submit_calls==1 && ad.scheduler.inflight==1 && ad.gmu.power_error==-EIO && !clear_halts);finish();

 setup(1);fault(0);new_fault_events=1;recover();dead(0);
 assert(ad.scheduler.fault==ADRENO_HWSCHED_FAULT_RESTART && !clear_halts && !ad.gmu.power_error);finish();

 setup(1);complete(0);recover();dead(0);assert(!submit_calls && !cancels && !ad.scheduler.fault);finish();

 setup(1);fault(0);complete(0);recover();dead(0);assert(!cancels && !submit_calls);finish();

 setup(1);fault(0);struct kgsl_mem_entry entry={0};commands[0].profiling_buf_entry=&entry;profile_fail=true;recover();dead(0);
 assert(profile_maps==1 && !profile_unmaps && cancels==1);finish();

 setup(1);fault(0);commands[0].profiling_buf_entry=&entry;recover();dead(0);assert(profile_maps==1 && profile_unmaps==1);finish();

 setup(0);recover();assert(!ad.scheduler.fault && suspend_calls==1 && boot_calls==1 && clear_halts==1);finish();

 printf("%u HWSCHED recovery/owner scenarios passed\n",cases);return 0;
}
