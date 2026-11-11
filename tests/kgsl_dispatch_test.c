/* Actual dispatcher/replay/stop/wait bodies; hardware/policy/events are modeled. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
typedef uint32_t u32;
#define BIT(n) (1U<<(n))
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define dev_err(dev,...) ((void)(dev))
#define ADRENO_SOFT_FAULT 1
#define ADRENO_HARD_FAULT 2
#define ADRENO_IOMMU_PAGE_FAULT 4
#define ADRENO_GMU_FAULT_SKIP_SNAPSHOT 8
#define KGSL_FT_PAGEFAULT_GPUHALT_ENABLE 0
#define ADRENO_DISPATCHER_POWER 0
#define ADRENO_DISPATCHER_ACTIVE 1
#define ADRENO_DEVICE_DRAWOBJ_PROFILE 2
#define CMDOBJ_PROFILE 0
#define CMDOBJ_FORCE_PREAMBLE 1
#define CMDOBJ_WFI 2
#define ADRENO_DEVICE_FAULT 4
#define ADRENO_DEVICE_STARTED 5
#define GFP_KERNEL 0
#define IS_ERR_OR_NULL(p) (!(p))
#define ADRENO_DRAWOBJ_PROFILE_COUNT 8
#define ADRENO_PREEMPT_NONE 0
#define ADRENO_DISPATCH_DRAWQUEUE_SIZE 8
#define KGSL_TIMESTAMP_RETIRED 1
#define DRAWQUEUE_NEXT(i,s) (((i)+1)%(s))
#define DRAWQUEUE(rb) (&(rb)->dispatch_q)
#define FOR_EACH_RINGBUFFER(a,rb,i) for((i)=0;(i)<(a)->num_ringbuffers && ((rb)=&(a)->ringbuffers[i],1);(i)++)
enum {ADRENO_REG_GMU_AO_HOST_INTERRUPT_MASK,ADRENO_REG_GMU_GMU2HOST_INTR_MASK,ADRENO_REG_RBBM_STATUS3,ADRENO_REG_CP_RB_BASE,ADRENO_REG_CP_RB_BASE_HI,ADRENO_REG_CP_ME_CNTL,ADRENO_REG_CP_IB1_BASE,ADRENO_REG_CP_IB1_BASE_HI};
struct kgsl_device;struct adreno_device;struct adreno_ringbuffer;
struct kthread_work {int unused;};struct timer_list {int n;};
struct kgsl_event_group {void *priv;};
struct adreno_dispatcher_drawqueue {struct kgsl_drawobj_cmd **retained_replay;unsigned retained_replay_count;int inflight,head,tail;unsigned long expires;struct kgsl_drawobj_cmd *cmd_q[8];};
struct kgsl_context {struct adreno_ringbuffer *rb;struct kgsl_event_group events;int id;bool detached,invalid;};
struct adreno_context {struct adreno_ringbuffer *rb;};
struct kgsl_drawobj {struct kgsl_context *context;unsigned timestamp;bool active;int destroyed;};
struct kgsl_drawobj_cmd {struct kgsl_drawobj base;unsigned long priv;int profile_index;uint64_t submit_ticks;};
struct buffer_desc {uint64_t gpuaddr;};
struct adreno_ringbuffer {struct adreno_dispatcher_drawqueue dispatch_q;struct buffer_desc *buffer_desc;struct kgsl_event_group events;struct adreno_device *owner;unsigned timestamp,id,wptr;int ts_expire_waitq;};
struct adreno_dispatcher {struct kthread_work work;struct timer_list timer,fault_timer;int fault,mutex,inflight;bool recovery_active;unsigned long priv;int idle_gate,jobs[2];};
struct kgsl_device {struct {int transition_error;} pwrctrl;int mutex,mmu,memstore;void *dev;};
struct adreno_gpudev {int (*clear_pending_transactions)(struct adreno_device *);void (*gpu_keepalive)(struct adreno_device *,bool);void (*preemption_schedule)(struct adreno_device *);};
struct adreno_device {struct kgsl_device dev;void *gpu_llc_slice,*gpuhtw_llc_slice;struct adreno_gpudev *gpudev;struct adreno_dispatcher dispatcher;struct {struct timer_list timer;} preempt;int num_ringbuffers,halt,profile_index;struct adreno_ringbuffer ringbuffers[2],*cur_rb,*prev_rb;unsigned long ft_pf_policy,priv;bool cooperative_reset;};
struct adreno_submit_time {uint64_t ktime,ticks;};
struct submission_info {int inflight,rb_id,rptr,wptr,gmu_dispatch_queue;};
#define KGSL_DEVICE(a) (&(a)->dev)
#define ADRENO_DEVICE(d) container_of(d,struct adreno_device,dev)
#define ADRENO_GPU_DEVICE(a) ((a)->gpudev)
#define DRAWOBJ(c) (&(c)->base)
#define ADRENO_CONTEXT(c) ((struct adreno_context *)(c))
#define ADRENO_DRAWOBJ_DISPATCH_DRAWQUEUE(o) (&(o)->context->rb->dispatch_q)
#define ADRENO_RB_DEVICE(rb) ((rb)->owner)
#define timer_container_of(var,t,member) container_of(t,__typeof__(*(var)),member)
static struct adreno_device gpu;
static struct adreno_gpudev gpudev;
static struct buffer_desc buffers[2];
static struct kgsl_drawobj_cmd commands[8];
static struct kgsl_context contexts[2];
static int reset_error,new_fault,reset_calls,retire_count,replay_count,cancel_count,event_groups,timer_updates,power_down,issued,reads,writes,mem_writes,submits,active_gets,active_puts,preempts,poison_at_lock;
static int timers_deleted,first_cancel_reset_calls,wait_retired,event_error,wait_result;
static int wakeups,waits,scenarios;
static int allocations,alloc_fail_at,submit_fail_at,submit_error,submit_fault_at,policy_invalidate,object_cancels,destroy_count;
static int stop_error,stop_checks,dispatcher_stops,ring_stops,ctx_clears,llcc_stops;
static bool awake,gx_on,stalled,preempt_enabled,gmu_enabled,soft_detect;
static unsigned retired_timestamp;
static unsigned adreno_ft_regs_val[2];static int adreno_ft_regs_num=2;
static unsigned long jiffies;
static unsigned adreno_drawobj_timeout=100,_fault_timer_interval=10;
static int adreno_dispatch_retire_drawqueue(struct adreno_device *,struct adreno_dispatcher_drawqueue *);
static void mutex_lock(int *m){assert(!*m);*m=1;if(m==&gpu.dev.mutex && poison_at_lock)gpu.dev.pwrctrl.transition_error=poison_at_lock;}
static void mutex_unlock(int *m){assert(*m);*m=0;}
static int atomic_xchg(int *p,int n){int old=*p;*p=n;return old;}
static void atomic_or(int n,int *p){*p|=n;}
static void atomic_add(int n,int *p){*p+=n;}
static int atomic_read(int *p){return *p;}
static void atomic_set(int *p,int v){*p=v;}
static void smp_wmb(void){}
static inline void adreno_clear_gpu_fault(struct adreno_device *a);
static bool test_bit(int b,unsigned long *p){return !!(*p&(1UL<<b));}
static void set_bit(int b,unsigned long *p){*p|=1UL<<b;}
static void clear_bit(int b,unsigned long *p){*p&=~(1UL<<b);}
static bool test_and_set_bit(int b,unsigned long *p){bool old=test_bit(b,p);set_bit(b,p);return old;}
static void timer_delete_sync(struct timer_list *t){timers_deleted++;}
static void mod_timer(struct timer_list *t,unsigned long when){timer_updates++;}
static unsigned long msecs_to_jiffies(unsigned long t){return t;}
static bool kgsl_state_is_awake(struct kgsl_device *d){return awake;}
static bool gmu_core_isenabled(struct kgsl_device *d){return gmu_enabled;}
static bool gmu_core_dev_gx_is_on(struct kgsl_device *d){return gx_on;}
static bool adreno_is_a5xx(struct adreno_device *a){return false;}
static bool adreno_is_a6xx(struct adreno_device *a){return true;}
static bool adreno_is_preemption_enabled(struct adreno_device *a){return preempt_enabled;}
static void adreno_write_gmureg(struct adreno_device *a,unsigned r,unsigned v){writes++;}
static void adreno_readreg(struct adreno_device *a,unsigned r,unsigned *v){reads++;*v=r==ADRENO_REG_RBBM_STATUS3 && stalled?BIT(24):0;}
static void adreno_writereg(struct adreno_device *a,unsigned r,unsigned v){writes++;}
static void adreno_readreg64(struct adreno_device *a,unsigned lo,unsigned hi,uint64_t *v){reads++;*v=lo==ADRENO_REG_CP_RB_BASE?buffers[0].gpuaddr:0x5555;}
static bool adreno_drawqueue_is_empty(struct adreno_dispatcher_drawqueue *q){return q->head==q->tail;}
static bool kgsl_check_timestamp(struct kgsl_device *d,struct kgsl_context *c,unsigned timestamp){return timestamp<=retired_timestamp;}
static void retire_cmdobj(struct adreno_device *a,struct kgsl_drawobj_cmd *c){retire_count++;c->base.active=false;c->base.destroyed++;}
static void trace_adreno_cmdbatch_fault(struct kgsl_drawobj_cmd *c,int fault){}
static void gmu_core_dev_cooperative_reset(struct kgsl_device *d){}
static void do_header_and_snapshot(struct kgsl_device *d,int f,struct adreno_ringbuffer *rb,struct kgsl_drawobj_cmd *c){}
static int resume_error, resume_calls;
static int kgsl_mmu_pagefault_resume(int *mmu){resume_calls++;return resume_error;}
static int adreno_gpu_halt(struct adreno_device *a){return a->halt;}
static void adreno_clear_gpu_halt(struct adreno_device *a){a->halt=0;}
static int adreno_gpu_fault(struct adreno_device *a){return a->dispatcher.fault;}
static int adreno_reset(struct kgsl_device *d,int fault){reset_calls++;gpu.dispatcher.fault|=new_fault;adreno_clear_gpu_fault(&gpu);if(!reset_error)for(unsigned i=0;i<ARRAY_SIZE(commands);i++)commands[i].base.active=false;return reset_error;}
static void kgsl_cancel_events(struct kgsl_device *d,struct kgsl_event_group *g){assert(reset_calls && !reset_error);if(!cancel_count)first_cancel_reset_calls=reset_calls;cancel_count++;}
/* Deliberately available to detect mutations restoring timestamp fabrication. */
void kgsl_sharedmem_writel(int memory,unsigned off,unsigned v){mem_writes++;}
static void *kcalloc(size_t n,size_t z,int flags){replay_count++;if(replay_count==alloc_fail_at)return NULL;void *p=calloc(n,z);assert(p);allocations++;return p;}
static void kfree(void *p){if(p){assert(allocations>0);allocations--;free(p);}}
static bool kgsl_context_detached(struct kgsl_context *c){return c->detached;}
static bool kgsl_context_invalid(struct kgsl_context *c){return c->invalid;}
static void kgsl_cancel_events_timestamp(struct kgsl_device *d,struct kgsl_event_group *g,unsigned ts){assert(reset_calls && !reset_error);object_cancels++;}
static void kgsl_drawobj_destroy(struct kgsl_drawobj *d){assert(!d->active && !d->destroyed);d->destroyed++;destroy_count++;}
static void process_cmdobj_fault(struct kgsl_device *d,struct kgsl_drawobj_cmd **replay,int count,unsigned base,int fault){assert(reset_calls && !reset_error && count>0);if(policy_invalidate)replay[0]->base.context->invalid=true;}
static int pending_transactions(struct adreno_device *a){stop_checks++;return stop_error;}
static void kgsl_pwrscale_update_stats(struct kgsl_device *d){}
static void adreno_irqctrl(struct adreno_device *a,int v){}
static void adreno_coresight_stop(struct adreno_device *a){}
static void adreno_perfcounter_save(struct adreno_device *a){}
static void adreno_dispatcher_stop(struct adreno_device *a){assert(!stop_error);dispatcher_stops++;}
static void adreno_ringbuffer_stop(struct adreno_device *a){assert(!stop_error);ring_stops++;}
static void llcc_slice_deactivate(void *p){assert(!stop_error);llcc_stops++;}
static void adreno_set_active_ctxs_null(struct adreno_device *a){assert(!stop_error);ctx_clears++;}
static void dispatcher_handle_jobs(struct adreno_device *a,int i){issued++;}
static int adreno_dispatch_process_drawqueue(struct adreno_device *a,struct adreno_dispatcher_drawqueue *q){return adreno_dispatch_retire_drawqueue(a,q);}
static void kgsl_process_event_groups(struct kgsl_device *d){event_groups++;}
static void _dispatcher_update_timers(struct adreno_device *a){timer_updates++;}
static void _dispatcher_power_down(struct adreno_device *a){power_down++;}
static int adreno_active_count_get(struct adreno_device *a){active_gets++;return 0;}
static void adreno_active_count_put(struct adreno_device *a){active_puts++;}
static int adreno_ringbuffer_submitcmd(struct adreno_device *a,struct kgsl_drawobj_cmd *c,struct adreno_submit_time *t){submits++;if(submits==submit_fail_at)return submit_error;c->base.active=true;if(submits==submit_fault_at)a->dispatcher.fault|=ADRENO_SOFT_FAULT;return 0;}
static void fault_detect_read(struct adreno_device *a){reads++;}
static void start_fault_timer(struct adreno_device *a){timer_updates++;}
static void reinit_completion(int *c){}
static void kgsl_pwrscale_midframe_timer_restart(struct kgsl_device *d){}
#define do_div(a,b) ((a)%=(b))
static unsigned adreno_get_rptr(struct adreno_ringbuffer *rb){return 0;}
static void trace_adreno_cmdbatch_submitted(struct kgsl_drawobj *d,struct submission_info *i,uint64_t t,unsigned long s,unsigned long n,int inflight){}
static bool adreno_in_preempt_state(struct adreno_device *a,int s){return false;}
static bool drawqueue_is_current(struct adreno_dispatcher_drawqueue *q){return true;}
static bool adreno_soft_fault_detect(struct adreno_device *a){return soft_detect;}
static bool fault_detect_read_compare(struct adreno_device *a){reads++;return true;}
static void adreno_set_gpu_fault(struct adreno_device *a,int f){a->dispatcher.fault|=f;}
static void adreno_dispatcher_schedule(struct kgsl_device *d){}
static void wake_up_all(int *wq){wakeups++;}
static bool adreno_ringbuffer_check_timestamp(struct adreno_ringbuffer *rb,unsigned ts,int type){return wait_retired;}
static int kgsl_add_event(struct kgsl_device *d,struct kgsl_event_group *g,unsigned ts,void (*cb)(struct kgsl_device *,struct kgsl_event_group *,void *,int),void *priv){return event_error;}
static bool kgsl_event_pending(struct kgsl_device *d,struct kgsl_event_group *g,unsigned ts,void (*cb)(struct kgsl_device *,struct kgsl_event_group *,void *,int),void *priv){return false;}
#define wait_event_timeout(wq,condition,t) ((void)(condition),(void)(t),waits++,wait_result)
#include "kgsl-dispatch-functions.h"
static void preempt_schedule(struct adreno_device *a){preempts++;}
static void setup(void)
{
    /* Fixture cleanup only; production retains failed replay owners. */
    for(int i=0;i<2;i++)kfree(gpu.ringbuffers[i].dispatch_q.retained_replay);
    assert(!allocations);
    memset(&gpu,0,sizeof(gpu));memset(commands,0,sizeof(commands));memset(contexts,0,sizeof(contexts));memset(&gpudev,0,sizeof(gpudev));
    gpu.gpudev=&gpudev;gpudev.preemption_schedule=preempt_schedule;gpu.num_ringbuffers=2;gpu.halt=3;gpu.dispatcher.fault=ADRENO_HARD_FAULT;gpu.dispatcher.inflight=2;
    for(int i=0;i<2;i++){
        struct adreno_ringbuffer *rb=&gpu.ringbuffers[i];rb->owner=&gpu;buffers[i].gpuaddr=0x1000+i*0x1000;rb->buffer_desc=&buffers[i];rb->events.priv=rb;rb->id=i;rb->timestamp=100+i;
        contexts[i].rb=rb;commands[i].base.context=&contexts[i];commands[i].base.timestamp=10+i;commands[i].base.active=true;contexts[i].id=i;
        rb->dispatch_q.inflight=1;rb->dispatch_q.tail=1;rb->dispatch_q.cmd_q[0]=&commands[i];
    }
    alloc_fail_at=submit_fail_at=submit_error=submit_fault_at=policy_invalidate=object_cancels=destroy_count=0;
    stop_error=stop_checks=dispatcher_stops=ring_stops=ctx_clears=llcc_stops=0;
    resume_error=resume_calls=0;
    reset_error=new_fault=reset_calls=retire_count=replay_count=cancel_count=event_groups=timer_updates=power_down=issued=reads=writes=mem_writes=submits=active_gets=active_puts=preempts=poison_at_lock=0;
    timers_deleted=first_cancel_reset_calls=wait_retired=event_error=wakeups=waits=0;wait_result=1;
    awake=gx_on=preempt_enabled=gmu_enabled=soft_detect=true;stalled=false;retired_timestamp=0;
}
static void preserved(void)
{
    assert(gpu.dispatcher.inflight==2 && gpu.halt==3 && !gpu.dev.mutex && !gpu.dispatcher.recovery_active);
    assert(!replay_count && !cancel_count && !mem_writes && !submits);
    for(int i=0;i<2;i++)assert(gpu.ringbuffers[i].dispatch_q.inflight==1 && gpu.ringbuffers[i].dispatch_q.head==0 && gpu.ringbuffers[i].dispatch_q.tail==1 && gpu.ringbuffers[i].dispatch_q.cmd_q[0]==&commands[i]);
}
int main(void)
{
    setup();reset_error=-ETIMEDOUT;assert(dispatcher_do_fault(&gpu)==-ETIMEDOUT);preserved();assert(reset_calls==1 && gpu.dev.pwrctrl.transition_error==-ETIMEDOUT && (gpu.dispatcher.fault&ADRENO_HARD_FAULT));scenarios++;
    int before_reads=reads,before_writes=writes;assert(dispatcher_do_fault(&gpu)==-ETIMEDOUT && reset_calls==1 && reads==before_reads && writes==before_writes);preserved();scenarios++;
    assert(adreno_gpu_stopped(&gpu));_adreno_dispatcher_issuecmds(&gpu);assert(!issued);scenarios++;
    assert(sendcmd(&gpu,&commands[0])==-ETIMEDOUT && !active_gets);preserved();scenarios++;
    adreno_dispatcher_work(&gpu.dispatcher.work);assert(!event_groups && !timer_updates && !power_down && !gpu.dispatcher.mutex);preserved();scenarios++;
    adreno_dispatcher_fault_timer(&gpu.dispatcher.fault_timer);assert(reads==before_reads && !timer_updates);scenarios++;
    setup();new_fault=ADRENO_SOFT_FAULT;assert(dispatcher_do_fault(&gpu)==-EIO);preserved();assert(gpu.dispatcher.fault==(ADRENO_HARD_FAULT|ADRENO_SOFT_FAULT));scenarios++;
    setup();reset_error=-EIO;new_fault=ADRENO_SOFT_FAULT;assert(dispatcher_do_fault(&gpu)==-EIO);preserved();assert(gpu.dispatcher.fault==3);scenarios++;
    setup();assert(dispatcher_do_fault(&gpu)==1);assert(reset_calls==1 && cancel_count==2 && first_cancel_reset_calls==1 && replay_count==2 && !mem_writes && !gpu.dev.pwrctrl.transition_error && !gpu.dispatcher.fault && gpu.halt==3 && !gpu.dev.mutex);scenarios++;
    setup();stalled=true;gpu.dispatcher.fault=ADRENO_SOFT_FAULT;assert(dispatcher_do_fault(&gpu)==-EBUSY);preserved();assert(!reset_calls && !gpu.dev.pwrctrl.transition_error && gpu.dispatcher.fault==ADRENO_SOFT_FAULT);scenarios++;
    setup();awake=false;assert(dispatcher_do_fault(&gpu)==-EIO);preserved();assert(!reset_calls && gpu.dev.pwrctrl.transition_error==-EIO);scenarios++;
    setup();awake=false;gpu.dispatcher.inflight=0;gpu.dispatcher.fault=ADRENO_SOFT_FAULT;assert(!dispatcher_do_fault(&gpu) && !reset_calls && !gpu.dev.pwrctrl.transition_error);scenarios++;
    setup();gpu.dispatcher.fault=0;assert(!dispatcher_do_fault(&gpu) && !reset_calls && !reads && !writes);scenarios++;
    setup();poison_at_lock=-EUCLEAN;assert(dispatcher_do_fault(&gpu)==-EUCLEAN);preserved();assert(!reset_calls && !reads && !writes && gpu.dispatcher.fault==ADRENO_HARD_FAULT);scenarios++;
    setup();retired_timestamp=10;reset_error=-EIO;assert(dispatcher_do_fault(&gpu)==-EIO && retire_count==1 && gpu.dispatcher.inflight==1 && gpu.ringbuffers[0].dispatch_q.cmd_q[0]==NULL && gpu.ringbuffers[1].dispatch_q.cmd_q[0]==&commands[1] && !cancel_count && !replay_count && !mem_writes);scenarios++;
    setup();reset_error=-EIO;adreno_dispatcher_work(&gpu.dispatcher.work);preserved();assert(event_groups==1 && !timer_updates && !power_down && !preempts && !issued && !gpu.dispatcher.mutex);scenarios++;
    setup();gpu.dispatcher.fault=0;gpu.halt=0;adreno_dispatcher_work(&gpu.dispatcher.work);assert(preempts==1 && issued==2 && timer_updates==1 && !reset_calls && !gpu.dispatcher.mutex);scenarios++;
    setup();gpu.halt=0;gpu.dispatcher.fault=0;gpu.dev.pwrctrl.transition_error=-EIO;assert(sendcmd(&gpu,&commands[0])==-EIO && !submits && !active_gets && gpu.dispatcher.inflight==2);scenarios++;
    setup();gpu.halt=0;gpu.dispatcher.fault=0;assert(!sendcmd(&gpu,&commands[0]) && submits==1 && gpu.dispatcher.inflight==3 && gpu.ringbuffers[0].dispatch_q.inflight==2);scenarios++;
    setup();gpu.dev.mutex=1;assert(adreno_ringbuffer_waittimestamp(&gpu.ringbuffers[0],77,100)==-EAGAIN && waits==1 && gpu.dev.mutex==1);gpu.dev.mutex=0;scenarios++;
    setup();gpu.dev.mutex=1;wait_retired=1;assert(!adreno_ringbuffer_waittimestamp(&gpu.ringbuffers[0],77,100) && gpu.dev.mutex==1);gpu.dev.mutex=0;scenarios++;
    setup();gpu.dev.mutex=1;wait_result=0;assert(adreno_ringbuffer_waittimestamp(&gpu.ringbuffers[0],77,100)==-ETIMEDOUT && gpu.dev.mutex==1);gpu.dev.mutex=0;scenarios++;
    setup();gpu.dev.mutex=1;event_error=-ENOMEM;assert(adreno_ringbuffer_waittimestamp(&gpu.ringbuffers[0],77,100)==-ENOMEM && !waits && gpu.dev.mutex==1);gpu.dev.mutex=0;scenarios++;
    setup();assert(adreno_ringbuffer_waittimestamp(&gpu.ringbuffers[0],77,0)==-EBUSY && !waits);scenarios++;
    setup();adreno_ringbuffer_wait_callback(&gpu.dev,&gpu.ringbuffers[0].events,NULL,1);assert(wakeups==1 && !mem_writes);scenarios++;
    setup();adreno_clear_gpu_fault(&gpu);assert(!gpu.dispatcher.fault);scenarios++;
    setup();gpu.dispatcher.recovery_active=true;adreno_clear_gpu_fault(&gpu);assert(gpu.dispatcher.fault==ADRENO_HARD_FAULT);scenarios++;
    setup();gpu.dispatcher.fault=ADRENO_IOMMU_PAGE_FAULT;stalled=true;resume_error=-ETIMEDOUT;assert(dispatcher_do_fault(&gpu)==-ETIMEDOUT);preserved();assert(resume_calls==1&&!reset_calls&&gpu.dev.pwrctrl.transition_error==-ETIMEDOUT&&gpu.dispatcher.fault==ADRENO_IOMMU_PAGE_FAULT);scenarios++;
    setup();gpu.dispatcher.fault=ADRENO_IOMMU_PAGE_FAULT;stalled=true;assert(dispatcher_do_fault(&gpu)==1&&resume_calls==1&&reset_calls==1&&replay_count==2&&!mem_writes);scenarios++;
    setup();alloc_fail_at=1;assert(dispatcher_do_fault(&gpu)==-ENOMEM);
    assert(gpu.halt==3 && gpu.dev.pwrctrl.transition_error==-ENOMEM && !submits && !destroy_count && !object_cancels && !allocations);
    assert(gpu.ringbuffers[0].dispatch_q.cmd_q[0]==&commands[0] && gpu.ringbuffers[1].dispatch_q.cmd_q[0]==&commands[1]);scenarios++;

    setup();alloc_fail_at=2;assert(dispatcher_do_fault(&gpu)==-ENOMEM);
    assert(submits==1 && commands[0].base.active && !destroy_count && gpu.dispatcher.inflight==1 && gpu.halt==3);
    assert(gpu.ringbuffers[1].dispatch_q.cmd_q[0]==&commands[1] && !gpu.ringbuffers[0].dispatch_q.retained_replay);scenarios++;

    setup();submit_fail_at=1;submit_error=-EIO;assert(dispatcher_do_fault(&gpu)==-EIO);
    struct adreno_dispatcher_drawqueue *q=&gpu.ringbuffers[0].dispatch_q;
    assert(q->retained_replay && q->retained_replay_count==1 && q->retained_replay[0]==&commands[0] && !q->inflight && !destroy_count && !object_cancels && gpu.halt==3);
    unsigned calls=submits;adreno_dispatcher_work(&gpu.dispatcher.work);assert(submits==calls && !destroy_count);scenarios++;

    setup();commands[2].base.context=&contexts[0];commands[2].base.timestamp=12;commands[2].base.active=true;
    q=&gpu.ringbuffers[0].dispatch_q;q->cmd_q[1]=&commands[2];q->tail=2;q->inflight=2;gpu.dispatcher.inflight=3;
    submit_fail_at=2;submit_error=-ENOSPC;assert(dispatcher_do_fault(&gpu)==-ENOSPC);
    assert(q->retained_replay_count==2 && !q->retained_replay[0] && q->retained_replay[1]==&commands[2]);
    assert(q->cmd_q[0]==&commands[0] && q->inflight==1 && gpu.dispatcher.inflight==1 && commands[0].base.active && !contexts[0].invalid && !object_cancels && !destroy_count);
    assert(gpu.ringbuffers[1].dispatch_q.cmd_q[0]==&commands[1] && gpu.halt==3);scenarios++;
    struct kgsl_drawobj_cmd **saved=q->retained_replay;assert(recover_dispatch_q(&gpu.dev,q,0,0)==-EUCLEAN && q->retained_replay==saved);scenarios++;

    setup();submit_fail_at=1;submit_error=-ENOENT;assert(dispatcher_do_fault(&gpu)==-ENOENT);
    assert(!object_cancels && !destroy_count && gpu.ringbuffers[0].dispatch_q.retained_replay[0]==&commands[0]);scenarios++;

    setup();policy_invalidate=1;assert(dispatcher_do_fault(&gpu)==1);
    assert(commands[0].base.destroyed==1 && !commands[0].base.active && commands[1].base.active && submits==1 && object_cancels==1 && !allocations && gpu.dispatcher.inflight==1);scenarios++;

    setup();contexts[0].detached=true;assert(dispatcher_do_fault(&gpu)==1);
    assert(destroy_count==1 && object_cancels==1 && submits==1 && !allocations);scenarios++;

    setup();submit_fault_at=1;assert(dispatcher_do_fault(&gpu)==-EPROTO);
    assert(commands[0].base.active && submits==1 && gpu.ringbuffers[0].dispatch_q.cmd_q[0]==&commands[0] && gpu.ringbuffers[1].dispatch_q.cmd_q[0]==&commands[1] && !destroy_count && gpu.halt==3 && gpu.dev.pwrctrl.transition_error==-EPROTO);scenarios++;

    setup();q=&gpu.ringbuffers[0].dispatch_q;q->head=7;q->tail=1;q->cmd_q[7]=&commands[0];q->cmd_q[0]=&commands[2];q->inflight=2;gpu.dispatcher.inflight=3;
    commands[2].base.context=&contexts[0];commands[2].base.timestamp=12;commands[2].base.active=true;
    assert(dispatcher_do_fault(&gpu)==1 && submits==3 && !allocations && q->cmd_q[0]==&commands[0] && q->cmd_q[1]==&commands[2] && q->inflight==2);scenarios++;

    setup();q=&gpu.ringbuffers[0].dispatch_q;q->inflight=2;
    assert(dispatcher_do_fault(&gpu)==-EUCLEAN && !submits && !allocations && q->cmd_q[0]==&commands[0]);scenarios++;

    setup();q=&gpu.ringbuffers[0].dispatch_q;q->cmd_q[0]=NULL;
    assert(recover_dispatch_q(&gpu.dev,q,0,0)==-EUCLEAN && !allocations);scenarios++;

    setup();q=&gpu.ringbuffers[0].dispatch_q;q->head=8;
    assert(recover_dispatch_q(&gpu.dev,q,0,0)==-EUCLEAN && !allocations);scenarios++;

    setup();q=&gpu.ringbuffers[0].dispatch_q;q->head=q->tail=0;q->inflight=0;
    assert(recover_dispatch_q(&gpu.dev,q,0,0)==0 && !replay_count && !allocations);scenarios++;

    setup();gpudev.clear_pending_transactions=pending_transactions;set_bit(ADRENO_DEVICE_STARTED,&gpu.priv);stop_error=-ETIMEDOUT;
    assert(adreno_stop(&gpu.dev)==-ETIMEDOUT && stop_checks==1 && !dispatcher_stops && !ring_stops && !ctx_clears && test_bit(ADRENO_DEVICE_STARTED,&gpu.priv));scenarios++;

    setup();gpudev.clear_pending_transactions=pending_transactions;set_bit(ADRENO_DEVICE_STARTED,&gpu.priv);gpu.gpu_llc_slice=gpu.gpuhtw_llc_slice=&gpu;
    assert(!adreno_stop(&gpu.dev) && stop_checks==1 && dispatcher_stops==1 && ring_stops==1 && ctx_clears==1 && llcc_stops==2 && !test_bit(ADRENO_DEVICE_STARTED,&gpu.priv));scenarios++;

    setup();set_bit(ADRENO_DEVICE_STARTED,&gpu.priv);assert(!adreno_stop(&gpu.dev) && !stop_checks && ctx_clears==1);scenarios++;
    setup();gpudev.clear_pending_transactions=pending_transactions;assert(!adreno_stop(&gpu.dev) && !stop_checks && !ctx_clears);scenarios++;

    printf("%d dispatcher/replay/stop/wait scenarios passed; reset/ringbuffer/hardware/events/fault-policy modeled\n",scenarios);
    return 0;
}
