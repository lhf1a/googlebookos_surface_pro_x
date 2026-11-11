/* Host harness: actual KGSL functions with modeled leaf dependencies. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
struct list_head { struct list_head *next, *prev; };
static void INIT_LIST_HEAD(struct list_head *h) { h->next=h->prev=h; }
static bool list_empty(const struct list_head *h) { return h->next==h; }
static void list_add_tail(struct list_head *n, struct list_head *h) { n->prev=h->prev; n->next=h; h->prev->next=n; h->prev=n; }
static void list_del(struct list_head *n) { n->prev->next=n->next; n->next->prev=n->prev; }
static void list_del_init(struct list_head *n) { list_del(n); INIT_LIST_HEAD(n); }
#define list_for_each_entry_safe(p,t,h,m) for (p=container_of((h)->next,__typeof__(*p),m), t=container_of(p->m.next,__typeof__(*p),m); &p->m!=(h); p=t,t=container_of(t->m.next,__typeof__(*t),m))
static void spin_lock(int *l) { assert(!*l); *l=1; }
static void spin_unlock(int *l) { assert(*l); *l=0; }
#define spin_lock_irqsave(l,f) do { (f)=0; spin_lock(l); } while(0)
#define spin_unlock_irqrestore(l,f) do { (void)(f); spin_unlock(l); } while(0)
struct kgsl_device; struct kgsl_event_group;
struct work_struct { void (*fn)(struct work_struct *); };
typedef void (*kgsl_event_func)(struct kgsl_device *, struct kgsl_event_group *, void *, int);
struct kgsl_context;
struct kgsl_event_group {
 struct kgsl_context *context; int lock;
 struct list_head events; unsigned int processed;
 void (*readtimestamp)(struct kgsl_device *, void *, int, unsigned int *);
 void *priv;
};
struct kgsl_sync_timeline { int lock; struct list_head child_list_head; };
struct kgsl_context { struct kgsl_event_group events; struct kgsl_sync_timeline *ktimeline; int refs; bool detached; unsigned long priv; unsigned int flags; int id; };
struct kgsl_device { void *events_wq; void *memstore; unsigned int queued,retired,started; };
struct kgsl_event { struct list_head node; int result; struct work_struct work; struct kgsl_device *device; struct kgsl_context *context; unsigned int timestamp; void *priv; kgsl_event_func func; unsigned long created; struct kgsl_event_group *group; };
struct dma_fence { int refs, error; bool signaled; int *lock; int callback_status; };
struct kgsl_sync_fence { struct dma_fence fence; struct kgsl_sync_timeline *parent; struct list_head child_list; unsigned int timestamp; };
struct kgsl_drawobj { unsigned int timestamp; int destroyed; };
#define ADRENO_CONTEXT_DRAWQUEUE_SIZE 8
struct adreno_context { struct kgsl_context base; int lock; struct kgsl_drawobj *drawqueue[8]; int drawqueue_head,drawqueue_tail; unsigned int timestamp; int waiting,wq,timeout; };
#define ADRENO_CONTEXT(c) container_of(c,struct adreno_context,base)
#define KGSL_EVENT_RETIRED 1
#define KGSL_EVENT_CANCELLED 2
#define KGSL_TIMESTAMP_RETIRED 1
#define KGSL_TIMESTAMP_QUEUED 2
#define KGSL_TIMESTAMP_WINDOW 0x80000000U
#define KGSL_CONTEXT_USER_GENERATED_TS 1
#define KGSL_CONTEXT_PRIV_INVALID 0
enum { soptimestamp, eoptimestamp };
#define KGSL_MEMSTORE_OFFSET(id,field) (field)
static void kgsl_sharedmem_writel(void *p,int field,unsigned int value) { struct kgsl_device *d=p; if(field==soptimestamp) d->started=value; else d->retired=value; }
#define GFP_KERNEL 0
#define KGSL_CONTEXT_ID(c) ((c)?(c)->id:0)
#define WARN_ON(x) (x)
#define trace_kgsl_fire_event(id,...) ((void)(id))
#define trace_kgsl_register_event(...) ((void)0)
#define trace_adreno_drawctxt_invalidate(...) ((void)0)
static unsigned long jiffies;
static void *events_cache;
static struct work_struct *work[64]; static unsigned nwork;
static int allocations, fail_alloc, wakeups;
static void *kmalloc(size_t s,int f) { if (fail_alloc && --fail_alloc==0) return NULL; void *p=calloc(1,s); assert(p); allocations++; return p; }
static void kfree(void *p) { if(p) { allocations--; free(p); } }
static void *kmem_cache_alloc(void *c,int f) { return kmalloc(sizeof(struct kgsl_event),f); }
static void kmem_cache_free(void *c,void *p) { kfree(p); }
#define INIT_WORK(w,f) ((w)->fn=(f))
static void queue_work(void *q,struct work_struct *w) { assert(nwork<64); work[nwork++]=w; }
static bool _kgsl_context_get(struct kgsl_context *c) { if(!c->refs) return false; c->refs++; return true; }
static void kgsl_context_put(struct kgsl_context *c) { if(c) { assert(c->refs>0); c->refs--; } }
static bool kgsl_context_invalid(struct kgsl_context *c) { return c->priv & 1; }
static bool kgsl_context_detached(struct kgsl_context *c) { return c->detached; }
static void set_bit(int n,unsigned long *p) { *p |= 1UL<<n; }
static int timestamp_cmp(unsigned int a,unsigned int b) { return (int32_t)(a-b); }
static void wake_up_all(int *p) { wakeups++; }
static void kgsl_drawobj_destroy(struct kgsl_drawobj *d) { assert(!d->destroyed); d->destroyed++; }
static struct dma_fence *dma_fence_get(struct dma_fence *f) { assert(f->refs>0); f->refs++; return f; }
static void dma_fence_put(struct dma_fence *f) { assert(f->refs>0); f->refs--; }
static bool dma_fence_is_signaled_locked(struct dma_fence *f) { assert(*f->lock && f->refs>0); return f->signaled; }
static void dma_fence_set_error(struct dma_fence *f,int error) { assert(*f->lock && !f->signaled && error<0); f->error=error; }
static void dma_fence_signal_locked(struct dma_fence *f) { assert(*f->lock && f->refs>0); f->signaled=true; f->callback_status=f->error?f->error:1; }
#include "fence-result-functions.h"
static void readts(struct kgsl_device *d,void *p,int type,unsigned int *ts) { *ts=type==KGSL_TIMESTAMP_RETIRED?d->retired:d->queued; }
static struct kgsl_device dev;
static struct adreno_context ctxt;
static struct kgsl_sync_timeline timeline;
static void setup(void) {
 assert(allocations==0 && nwork==0);
 memset(&dev,0,sizeof(dev)); memset(&ctxt,0,sizeof(ctxt)); memset(&timeline,0,sizeof(timeline));
 dev.memstore=&dev; dev.queued=100; dev.retired=5; dev.started=7; ctxt.base.refs=1; ctxt.base.ktimeline=&timeline;
 ctxt.base.events.context=&ctxt.base; ctxt.base.events.readtimestamp=readts;
 INIT_LIST_HEAD(&ctxt.base.events.events); INIT_LIST_HEAD(&timeline.child_list_head);
 ctxt.timestamp=100; fail_alloc=0; wakeups=0;
}
static void init_fence(struct kgsl_sync_fence *f,unsigned ts) {
 memset(f,0,sizeof(*f)); f->parent=&timeline; f->timestamp=ts; f->fence.refs=2; f->fence.lock=&timeline.lock;
 list_add_tail(&f->child_list,&timeline.child_list_head);
}
static void run_work(unsigned index) {
 assert(index<nwork); struct work_struct *w=work[index];
 memmove(&work[index],&work[index+1],(--nwork-index)*sizeof(work[0]));
 w->fn(w);
}
static void flush_work(void) { while(nwork) run_work(0); }
static void user_close(struct kgsl_sync_fence *f) { dma_fence_put(&f->fence); }
static void finish(struct kgsl_sync_fence *f) { user_close(f); assert(f->fence.refs==0); }
static unsigned cases;
#define PASS() do { assert(allocations==0 && nwork==0 && ctxt.base.refs==1); assert(list_empty(&ctxt.base.events.events)); assert(list_empty(&timeline.child_list_head)); cases++; } while(0)
int main(void) {
 (void)kgsl_sharedmem_writel;
 struct kgsl_sync_fence a,b;
 setup(); init_fence(&a,10); assert(kgsl_enable_signaling(&a.fence));
 assert(!_add_fence_event(&dev,&ctxt.base,&a)); assert(a.fence.refs==3);
 assert(!a.fence.signaled && nwork==0); dev.retired=10; kgsl_flush_event_group(&dev,&ctxt.base.events); flush_work();
 assert(a.fence.callback_status==1); finish(&a); PASS();

 setup(); init_fence(&a,10); assert(!_add_fence_event(&dev,&ctxt.base,&a));
 kgsl_cancel_events(&dev,&ctxt.base.events); flush_work();
 assert(a.fence.signaled && a.fence.callback_status==-ECANCELED && dev.retired==5); finish(&a); PASS();

 /* A later success callback must not preempt an earlier cancellation. */
 setup(); init_fence(&a,10); init_fence(&b,20);
 assert(!_add_fence_event(&dev,&ctxt.base,&a)); assert(!_add_fence_event(&dev,&ctxt.base,&b));
 kgsl_cancel_events_timestamp(&dev,&ctxt.base.events,10); dev.retired=20;
 kgsl_flush_event_group(&dev,&ctxt.base.events); assert(nwork==2); run_work(1);
 assert(b.fence.callback_status==1 && !a.fence.signaled); run_work(0);
 assert(a.fence.callback_status==-ECANCELED); finish(&a); finish(&b); PASS();

 /* A cancellation never completes another pending timestamp. */
 setup(); init_fence(&a,10); init_fence(&b,20);
 assert(!_add_fence_event(&dev,&ctxt.base,&a)); assert(!_add_fence_event(&dev,&ctxt.base,&b));
 kgsl_cancel_events_timestamp(&dev,&ctxt.base.events,20); flush_work();
 assert(b.fence.callback_status==-ECANCELED && !a.fence.signaled);
 dev.retired=10; kgsl_flush_event_group(&dev,&ctxt.base.events); flush_work();
 assert(a.fence.callback_status==1); finish(&a); finish(&b); PASS();

 /* Callback owns the fence even after ioctl unwind and userspace close. */
 setup(); init_fence(&a,10); assert(!_add_fence_event(&dev,&ctxt.base,&a));
 kgsl_sync_cancel(&a); user_close(&a); assert(a.fence.refs==1);
 kgsl_cancel_events(&dev,&ctxt.base.events); flush_work();
 assert(a.fence.refs==0 && a.fence.callback_status==-ECANCELED); PASS();

 setup(); init_fence(&a,10); fail_alloc=1;
 assert(_add_fence_event(&dev,&ctxt.base,&a)==-ENOMEM); assert(a.fence.refs==2);
 kgsl_sync_cancel(&a); finish(&a); PASS();

 setup(); init_fence(&a,10); fail_alloc=2;
 assert(_add_fence_event(&dev,&ctxt.base,&a)==-ENOMEM); assert(a.fence.refs==2);
 kgsl_sync_cancel(&a); finish(&a); PASS();

 setup(); init_fence(&a,101);
 assert(_add_fence_event(&dev,&ctxt.base,&a)==-EINVAL); assert(a.fence.refs==2);
 kgsl_sync_cancel(&a); finish(&a); PASS();

 setup(); init_fence(&a,10); ctxt.base.refs=0;
 assert(_add_fence_event(&dev,&ctxt.base,&a)==-ENOENT); assert(a.fence.refs==2);
 ctxt.base.refs=1; kgsl_sync_cancel(&a); finish(&a); PASS();

 setup(); init_fence(&a,10); ctxt.base.priv=1;
 assert(!_add_fence_event(&dev,&ctxt.base,&a)); assert(nwork==1 && list_empty(&ctxt.base.events.events));
 flush_work(); assert(a.fence.callback_status==-ECANCELED); finish(&a); PASS();

 setup(); init_fence(&a,10); ctxt.base.detached=true;
 assert(!_add_fence_event(&dev,&ctxt.base,&a)); assert(nwork==1 && list_empty(&ctxt.base.events.events));
 flush_work(); assert(a.fence.callback_status==-ECANCELED); finish(&a); PASS();

 setup(); init_fence(&a,5); ctxt.base.priv=1;
 assert(!_add_fence_event(&dev,&ctxt.base,&a)); flush_work();
 assert(a.fence.callback_status==1); finish(&a); PASS();

 /* Invalidation cancels unsent commands without changing either GPU word. */
 setup(); init_fence(&a,5); init_fence(&b,20);
 assert(!_add_fence_event(&dev,&ctxt.base,&a)); assert(!_add_fence_event(&dev,&ctxt.base,&b));
 struct kgsl_drawobj queued={.timestamp=20}; ctxt.drawqueue[0]=&queued; ctxt.drawqueue_tail=1;
 adreno_drawctxt_invalidate(&dev,&ctxt.base);
 assert(dev.retired==5 && dev.started==7 && queued.destroyed==1 && wakeups==3);
 assert(kgsl_context_invalid(&ctxt.base)); flush_work();
 assert(a.fence.callback_status==1 && b.fence.callback_status==-ECANCELED); finish(&a); finish(&b); PASS();

 setup(); init_fence(&a,10); kgsl_sync_fence_signal(&a,777);
 assert(a.fence.callback_status==-EIO); kgsl_sync_fence_signal(&a,KGSL_EVENT_RETIRED);
 assert(a.fence.callback_status==-EIO && a.fence.refs==1); finish(&a); PASS();

 setup(); init_fence(&a,5); kgsl_sync_fence_signal(&a,KGSL_EVENT_RETIRED);
 kgsl_sync_fence_signal(&a,KGSL_EVENT_CANCELLED);
 assert(a.fence.callback_status==1 && a.fence.refs==1); finish(&a); PASS();

 /* Wraparound is decided by event timestamp comparisons, not a timeline. */
 setup(); dev.retired=0xfffffff0U; dev.queued=4; init_fence(&a,0xfffffffeU); init_fence(&b,2);
 assert(!_add_fence_event(&dev,&ctxt.base,&a)); assert(!_add_fence_event(&dev,&ctxt.base,&b));
 dev.retired=1; kgsl_flush_event_group(&dev,&ctxt.base.events); flush_work();
 assert(a.fence.callback_status==1 && b.fence.callback_status==-ECANCELED); finish(&a); finish(&b); PASS();

 printf("%u KGSL fence/event/invalidation scenarios passed\n",cases);
 return 0;
}
