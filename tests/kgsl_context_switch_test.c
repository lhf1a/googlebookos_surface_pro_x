/* Actual switch/event functions; hardware, workqueue, lock and ref leaves modeled. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
struct list_head {struct list_head *next,*prev;};
static void INIT_LIST_HEAD(struct list_head *h){h->next=h->prev=h;}
static bool list_empty(const struct list_head *h){return h->next==h;}
static void list_add_tail(struct list_head *n,struct list_head *h){n->prev=h->prev;n->next=h;h->prev->next=n;h->prev=n;}
#define LIST_HEAD(name) struct list_head name={&(name),&(name)}
static void list_del(struct list_head *n){n->prev->next=n->next;n->next->prev=n->prev;}
static void list_move_tail(struct list_head *n,struct list_head *h){list_del(n);list_add_tail(n,h);}
#define FOR_EACH_RINGBUFFER(a,r,i) for((i)=0;(i)<2 && ((r)=&(a)->ringbuffers[i]);(i)++)
#define list_for_each_entry_safe(p,t,h,m) for(p=container_of((h)->next,__typeof__(*p),m),t=container_of(p->m.next,__typeof__(*p),m); &p->m!=(h);p=t,t=container_of(t->m.next,__typeof__(*t),m))
struct work_struct {void (*fn)(struct work_struct *);};
struct kgsl_device;struct kgsl_event_group;struct kgsl_context;
typedef void (*kgsl_event_func)(struct kgsl_device *,struct kgsl_event_group *,void *,int);
enum kgsl_timestamp_type {KGSL_TIMESTAMP_RETIRED=1,KGSL_TIMESTAMP_QUEUED};
struct kgsl_event_group {struct kgsl_context *context;int lock;struct list_head events,group;unsigned processed;int (*readtimestamp)(struct kgsl_device *,void *,enum kgsl_timestamp_type,unsigned *);void *priv;};
struct kgsl_event {struct list_head node;int result;struct work_struct work;struct kgsl_device *device;struct kgsl_context *context;unsigned timestamp;void *priv;kgsl_event_func func;unsigned long created;struct kgsl_event_group *group;};
struct kgsl_pagetable {int id;};
struct kgsl_process_private {struct kgsl_pagetable *pagetable;};
struct kgsl_context {int refs;bool detached,invalid;unsigned flags;int id;struct kgsl_process_private *proc_priv;};
struct adreno_context {struct kgsl_context base;};
struct kgsl_memdesc {int value;};
struct adreno_device;
struct kgsl_device {struct {struct kgsl_pagetable *defaultpagetable;} mmu;struct {int transition_error;} pwrctrl;struct adreno_device *adreno;void *events_wq;};
struct adreno_ringbuffer {struct kgsl_event_group events;struct adreno_context *drawctxt_active;struct list_head context_switches;unsigned timestamp,retired;int id,ts_expire_waitq,preempt_lock;struct kgsl_memdesc *pagetable_desc,*profile_desc,*buffer_desc;};
struct adreno_device {struct kgsl_device dev;struct adreno_ringbuffer ringbuffers[2];};
#define KGSL_DEVICE(a) (&(a)->dev)
#define ADRENO_DEVICE(d) ((d)->adreno)
static struct adreno_device gpu;
#define ADRENO_RB_DEVICE(rb) (&gpu)
#define ADRENO_FEATURE(a,f) false
#define KGSL_EVENT_RETIRED 1
#define KGSL_EVENT_CANCELLED 2
#define KGSL_CONTEXT_USER_GENERATED_TS 1
#define KGSL_TIMESTAMP_WINDOW 0x80000000U
#define KGSL_MMU_TYPE_NONE 0
#define KGSL_CMD_FLAGS_PMODE 1
#define KGSL_MEMDESC_PRIVILEGED 1
#define KGSL_MEMFLAGS_GPUREADONLY 1
#define KGSL_RB_SIZE 32768
#define PAGE_SIZE 4096
#define SZ_16K 16384
#define SZ_4K 4096
#define GFP_KERNEL 0
#define KGSL_CONTEXT_ID(c) ((c)?(c)->id:0)
#define WARN_ON(x) (x)
#define WARN(x,...) assert(!(x))
#define IS_ERR(p) ((uintptr_t)(p)>=(uintptr_t)-4095)
#define IS_ERR_OR_NULL(p) (!(p)||IS_ERR(p))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define trace_kgsl_fire_event(id,...) ((void)(id))
#define trace_kgsl_register_event(...) ((void)0)
#define trace_adreno_drawctxt_switch(...) ((void)0)
static unsigned long jiffies;
static void *events_cache;
static struct work_struct *work[64];static unsigned nwork;
static int allocations,fail_alloc,submit_error,submits,generated;
static bool eager,retire_on_submit,reset_on_unlock;
void adreno_drawctxt_retire_switches(struct adreno_device *adreno_dev);
static void run_work(unsigned index);
static void spin_lock(int *l){assert(!*l);*l=1;}
static void spin_unlock(int *l){assert(*l);*l=0;if(reset_on_unlock){reset_on_unlock=false;adreno_drawctxt_retire_switches(&gpu);}if(eager&&nwork)run_work(0);}
static void spin_lock_init(int *l){*l=0;}
static void init_waitqueue_head(int *w){*w=0;}
static void *kmalloc(size_t s,int flags){if(fail_alloc&&--fail_alloc==0)return NULL;void *p=calloc(1,s);assert(p);allocations++;return p;}
static void *kzalloc(size_t s,int flags){return kmalloc(s,flags);}
static void kfree(void *p){if(p){allocations--;free(p);}}
static void *kmem_cache_alloc(void *c,int f){return kmalloc(sizeof(struct kgsl_event),f);}
static void kmem_cache_free(void *c,void *p){kfree(p);}
#define INIT_WORK(w,f) ((w)->fn=(f))
static void queue_work(void *q,struct work_struct *w){assert(nwork<64);work[nwork++]=w;}
static bool _kgsl_context_get(struct kgsl_context *c){if(!c->refs)return false;c->refs++;return true;}
static void kgsl_context_put(struct kgsl_context *c){if(c){assert(c->refs>0);c->refs--;}}
static bool kgsl_context_invalid(struct kgsl_context *c){return c->invalid;}
static bool kgsl_context_detached(struct kgsl_context *c){return c->detached;}
static int timestamp_cmp(unsigned a,unsigned b){return (int32_t)(a-b);}
static int kgsl_mmu_get_mmutype(struct kgsl_device *d){return 1;}
static unsigned adreno_iommu_set_pt_generate_cmds(struct adreno_ringbuffer *r,unsigned *c,struct kgsl_pagetable *p){assert(p);generated++;return 4;}
static unsigned __add_curr_ctxt_cmds(struct adreno_ringbuffer *r,unsigned *c,struct adreno_context *d){return 4;}
static int adreno_ringbuffer_issue_internal_cmds(struct adreno_ringbuffer *r,unsigned flags,unsigned *c,int count){assert(flags==KGSL_CMD_FLAGS_PMODE&&count>=4);if(submit_error)return submit_error;submits++;r->timestamp++;if(retire_on_submit)r->retired=r->timestamp;return 0;}
static struct kgsl_memdesc globals[6];static unsigned globals_used;
static struct kgsl_memdesc *kgsl_allocate_global(struct kgsl_device *d,unsigned size,unsigned align,unsigned flags,unsigned priv,const char *name){assert(globals_used<6);return &globals[globals_used++];}
static int adreno_rb_readtimestamp(struct adreno_device *a,void *p,enum kgsl_timestamp_type type,unsigned *ts){struct adreno_ringbuffer *r=p;*ts=type==KGSL_TIMESTAMP_RETIRED?r->retired:r->timestamp;return 0;}
static void kgsl_add_event_group(struct kgsl_device *d,struct kgsl_event_group *g,struct kgsl_context *c,int (*readts)(struct kgsl_device *,void *,enum kgsl_timestamp_type,unsigned *),void *p,const char *fmt,int id){memset(g,0,sizeof(*g));g->context=c;g->readtimestamp=readts;g->priv=p;INIT_LIST_HEAD(&g->events);INIT_LIST_HEAD(&g->group);}
#include "context-switch-functions.h"
static struct kgsl_pagetable pts[4];static struct kgsl_process_private procs[3];static struct adreno_context ctx[3];
static struct adreno_ringbuffer *rb;
static void run_work(unsigned index){assert(index<nwork);struct work_struct *w=work[index];memmove(&work[index],&work[index+1],(--nwork-index)*sizeof(work[0]));w->fn(w);}
static void flush_work(void){while(nwork)run_work(0);}
static void setup(void){assert(!allocations&&!nwork);memset(&gpu,0,sizeof(gpu));for(int i=0;i<2;i++)INIT_LIST_HEAD(&gpu.ringbuffers[i].events.group);memset(ctx,0,sizeof(ctx));gpu.dev.adreno=&gpu;gpu.dev.mmu.defaultpagetable=&pts[3];globals_used=0;assert(!_adreno_ringbuffer_init(&gpu,0));assert(!_adreno_ringbuffer_init(&gpu,1));rb=&gpu.ringbuffers[0];for(int i=0;i<3;i++){procs[i].pagetable=&pts[i];ctx[i].base=(struct kgsl_context){.refs=1,.id=i+1,.proc_priv=&procs[i]};}ctx[0].base.refs++;rb->drawctxt_active=&ctx[0];rb->timestamp=10;rb->retired=5;fail_alloc=submit_error=submits=generated=0;eager=retire_on_submit=reset_on_unlock=false;}
static unsigned owners(struct adreno_ringbuffer *r){unsigned n=0;for(struct list_head *h=r->context_switches.next;h!=&r->context_switches;h=h->next)n++;return n;}
static struct adreno_context_switch *first_owner(struct adreno_ringbuffer *r){assert(!list_empty(&r->context_switches));return container_of(r->context_switches.next,struct adreno_context_switch,node);}
static void retire(struct adreno_ringbuffer *r){r->retired=r->timestamp;kgsl_flush_event_group(&gpu.dev,&r->events);flush_work();}
/* Fixture teardown only: never claimed as a driver retirement/recovery path. */
static void cleanup(void){eager=false;for(unsigned i=0;i<2;i++){struct adreno_ringbuffer *r=&gpu.ringbuffers[i];kgsl_cancel_events(&gpu.dev,&r->events);}flush_work();for(unsigned i=0;i<2;i++){struct adreno_ringbuffer *r=&gpu.ringbuffers[i];while(!list_empty(&r->context_switches)){struct adreno_context_switch *s=first_owner(r);list_del(&s->node);kgsl_context_put(&s->drawctxt->base);kfree(s);}if(r->drawctxt_active)kgsl_context_put(&r->drawctxt_active->base);r->drawctxt_active=NULL;}assert(!allocations&&!nwork);for(unsigned i=0;i<3;i++)assert(ctx[i].base.refs==1);}
static unsigned cases;
#define PASS() do{cleanup();cases++;}while(0)
int main(void){
 setup();assert(!adreno_drawctxt_switch(&gpu,rb,&ctx[1]));assert(submits==1&&rb->drawctxt_active==&ctx[1]&&owners(rb)==1&&ctx[0].base.refs==2&&ctx[1].base.refs==2);assert(first_owner(rb)->timestamp==11);retire(rb);assert(!owners(rb)&&ctx[0].base.refs==1&&ctx[1].base.refs==2);PASS();
 setup();assert(!adreno_drawctxt_switch(&gpu,rb,&ctx[0])&&!submits&&!allocations);PASS();
 setup();assert(adreno_drawctxt_switch(&gpu,NULL,&ctx[1])==-EINVAL&&!submits);PASS();
 setup();ctx[1].base.detached=true;assert(adreno_drawctxt_switch(&gpu,rb,&ctx[1])==-ENOENT&&!submits);PASS();
 setup();ctx[1].base.refs=0;assert(adreno_drawctxt_switch(&gpu,rb,&ctx[1])==-ENOENT&&!submits&&!allocations);ctx[1].base.refs=1;PASS();
 setup();fail_alloc=1;assert(adreno_drawctxt_switch(&gpu,rb,&ctx[1])==-ENOMEM&&!submits&&!owners(rb)&&rb->drawctxt_active==&ctx[0]&&ctx[1].base.refs==1);PASS();
 setup();fail_alloc=2;assert(adreno_drawctxt_switch(&gpu,rb,&ctx[1])==-ENOMEM&&!submits&&!owners(rb)&&rb->drawctxt_active==&ctx[0]&&ctx[1].base.refs==1);PASS();
 setup();submit_error=-ENOSPC;assert(adreno_drawctxt_switch(&gpu,rb,&ctx[1])==-ENOSPC&&!submits&&!owners(rb)&&rb->drawctxt_active==&ctx[0]&&ctx[1].base.refs==1);PASS();
 setup();fail_alloc=3;assert(adreno_drawctxt_switch(&gpu,rb,&ctx[1])==-ENOMEM&&submits==1&&owners(rb)==1&&rb->drawctxt_active==&ctx[1]);assert(ctx[0].base.refs==2&&ctx[1].base.refs==2&&first_owner(rb)->result==-ENOMEM&&gpu.dev.pwrctrl.transition_error==-ENOMEM);assert(adreno_drawctxt_switch(&gpu,rb,&ctx[1])==-ENOMEM&&submits==1);PASS();
 setup();gpu.dev.pwrctrl.transition_error=-EIO;assert(adreno_drawctxt_switch(&gpu,rb,&ctx[0])==-EIO&&!submits&&!allocations);PASS();
 setup();assert(!adreno_drawctxt_switch(&gpu,rb,&ctx[1]));kgsl_cancel_events(&gpu.dev,&rb->events);flush_work();assert(owners(rb)==1&&ctx[0].base.refs==2&&first_owner(rb)->result==KGSL_EVENT_CANCELLED);rb->retired=rb->timestamp;kgsl_flush_event_group(&gpu.dev,&rb->events);flush_work();assert(owners(rb)==1&&ctx[0].base.refs==2);PASS();
 setup();assert(!adreno_drawctxt_switch(&gpu,rb,&ctx[1]));kgsl_cancel_events(&gpu.dev,&rb->events);assert(nwork==1);container_of(work[0],struct kgsl_event,work)->result=777;flush_work();assert(owners(rb)==1&&ctx[0].base.refs==2&&first_owner(rb)->result==777);PASS();
 setup();retire_on_submit=eager=true;assert(!adreno_drawctxt_switch(&gpu,rb,&ctx[1])&&!owners(rb)&&!nwork&&ctx[0].base.refs==1&&ctx[1].base.refs==2);PASS();
 setup();assert(!adreno_drawctxt_switch(&gpu,rb,NULL)&&rb->drawctxt_active==NULL&&owners(rb)==1);retire(rb);assert(ctx[0].base.refs==1);PASS();
 setup();kgsl_context_put(&ctx[0].base);rb->drawctxt_active=NULL;assert(!adreno_drawctxt_switch(&gpu,rb,&ctx[1])&&submits==1&&!owners(rb)&&ctx[1].base.refs==2);PASS();
 setup();procs[1].pagetable=procs[0].pagetable;assert(!adreno_drawctxt_switch(&gpu,rb,&ctx[1])&&!generated&&owners(rb)==1);retire(rb);assert(ctx[0].base.refs==1);PASS();
 setup();assert(!adreno_drawctxt_switch(&gpu,rb,&ctx[1]));assert(!adreno_drawctxt_switch(&gpu,rb,&ctx[2]));assert(owners(rb)==2);kgsl_cancel_events_timestamp(&gpu.dev,&rb->events,11);rb->retired=12;kgsl_flush_event_group(&gpu.dev,&rb->events);assert(nwork==2);run_work(1);assert(owners(rb)==1&&ctx[1].base.refs==1&&ctx[0].base.refs==2);run_work(0);assert(owners(rb)==1&&ctx[0].base.refs==2);PASS();
 setup();assert(!adreno_drawctxt_switch(&gpu,rb,&ctx[1]));assert(!adreno_drawctxt_switch(&gpu,rb,&ctx[0]));assert(ctx[0].base.refs==3&&owners(rb)==2);retire(rb);assert(!owners(rb)&&ctx[0].base.refs==2&&ctx[1].base.refs==1);PASS();
 setup();rb->timestamp=0xfffffffeU;rb->retired=0xfffffffdU;assert(!adreno_drawctxt_switch(&gpu,rb,&ctx[1]));assert(!adreno_drawctxt_switch(&gpu,rb,&ctx[2]));assert(rb->timestamp==0&&owners(rb)==2);retire(rb);assert(!owners(rb)&&ctx[0].base.refs==1&&ctx[1].base.refs==1);PASS();
 setup();assert(!adreno_drawctxt_switch(&gpu,rb,&ctx[1]));assert(_adreno_ringbuffer_init(&gpu,0)==-EBUSY&&owners(rb)==1&&!list_empty(&rb->events.events));kgsl_cancel_events(&gpu.dev,&rb->events);flush_work();assert(_adreno_ringbuffer_init(&gpu,0)==-EBUSY&&owners(rb)==1);PASS();
 setup();assert(!adreno_drawctxt_switch(&gpu,rb,&ctx[1]));struct adreno_ringbuffer *second=&gpu.ringbuffers[1];assert(!adreno_drawctxt_switch(&gpu,second,&ctx[2]));assert(!adreno_drawctxt_switch(&gpu,second,&ctx[1]));assert(owners(rb)==1&&owners(second)==1);retire(second);assert(owners(rb)==1&&!owners(second)&&ctx[2].base.refs==1);PASS();
 setup();assert(!adreno_drawctxt_switch(&gpu,rb,&ctx[1]));ctx[0].base.detached=true;kgsl_context_put(&ctx[0].base);assert(ctx[0].base.refs==1);retire(rb);assert(ctx[0].base.refs==0&&!owners(rb));ctx[0].base.refs=1;PASS();
 setup();assert(!adreno_drawctxt_switch(&gpu,rb,&ctx[1]));ctx[0].base.detached=true;kgsl_context_put(&ctx[0].base);kgsl_cancel_events(&gpu.dev,&rb->events);flush_work();assert(ctx[0].base.refs==1&&owners(rb)==1);ctx[0].base.refs++;PASS();
 /* Checked-reset release while the cancellation callback is still queued. */
 setup();assert(!adreno_drawctxt_switch(&gpu,rb,&ctx[1]));adreno_drawctxt_retire_switches(&gpu);assert(owners(rb)==1&&ctx[0].base.refs==2&&nwork==1);flush_work();assert(!owners(rb)&&ctx[0].base.refs==1);PASS();
 setup();assert(!adreno_drawctxt_switch(&gpu,rb,&ctx[1]));kgsl_cancel_events(&gpu.dev,&rb->events);flush_work();assert(owners(rb)==1);adreno_drawctxt_retire_switches(&gpu);assert(!owners(rb)&&ctx[0].base.refs==1&&!nwork);PASS();
 setup();fail_alloc=3;assert(adreno_drawctxt_switch(&gpu,rb,&ctx[1])==-ENOMEM);adreno_drawctxt_retire_switches(&gpu);assert(!owners(rb)&&ctx[0].base.refs==1&&ctx[1].base.refs==2&&gpu.dev.pwrctrl.transition_error==-ENOMEM);PASS();
 setup();assert(!adreno_drawctxt_switch(&gpu,rb,&ctx[1]));rb->retired=rb->timestamp;kgsl_flush_event_group(&gpu.dev,&rb->events);assert(nwork==1);adreno_drawctxt_retire_switches(&gpu);flush_work();assert(!owners(rb)&&ctx[0].base.refs==1);PASS();
 setup();assert(!adreno_drawctxt_switch(&gpu,rb,&ctx[1]));assert(!adreno_drawctxt_switch(&gpu,rb,&ctx[2]));kgsl_cancel_events_timestamp(&gpu.dev,&rb->events,11);flush_work();eager=true;adreno_drawctxt_retire_switches(&gpu);assert(!owners(rb)&&!nwork&&ctx[0].base.refs==1&&ctx[1].base.refs==1);adreno_drawctxt_retire_switches(&gpu);PASS();
 setup();assert(!adreno_drawctxt_switch(&gpu,rb,&ctx[1]));struct adreno_ringbuffer *r2=&gpu.ringbuffers[1];assert(!adreno_drawctxt_switch(&gpu,r2,&ctx[2]));assert(!adreno_drawctxt_switch(&gpu,r2,&ctx[1]));adreno_drawctxt_retire_switches(&gpu);assert(nwork==2);run_work(1);run_work(0);assert(!owners(rb)&&!owners(r2)&&ctx[0].base.refs==1&&ctx[2].base.refs==1);PASS();
 /* Reclaim immediately after cancelled callback unlocks, before it returns. */
 setup();assert(!adreno_drawctxt_switch(&gpu,rb,&ctx[1]));kgsl_cancel_events(&gpu.dev,&rb->events);reset_on_unlock=true;flush_work();assert(!owners(rb)&&ctx[0].base.refs==1&&!reset_on_unlock);PASS();
 printf("%u context-switch/event/ownership scenarios passed; CP, locks and references modeled\n",cases);
 return 0;
}
