/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Actual HFI probe, cleanup, packet producers and worker loop are extracted.
 * pthreads execute the real blocking wait predicate and stop/join ordering.
 * Kernel data types, IRQ request, slab allocation, waits and event processing
 * below are models: no hardware IRQ, MMIO, DMA, GPU completion or kernel
 * concurrency is validated. The NO_AUTOEN contract is also source-checked.
 *
 * Scenarios: blocked worker stop; name/cache/task/IRQ failure and same-instance
 * retry; task-only/cache-only retained guards; two independent instances and
 * both message queues; duplicate probe; allocation failure; independent close
 * and surviving instance use; fresh probe. Successful-probe cleanup in this
 * harness is explicit model cleanup with no real IRQ producer, NOT an exported
 * product runtime-unbind implementation. No fences or timestamps are invented.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <stddef.h>
typedef uint32_t u32;
#define MAX_RCVD_SIZE 16
#define HFI_IRQ_MASK 7
#define GFP_ATOMIC 0
#define GFP_KERNEL 1
#define F2H_MSG_TS_RETIRE 1
#define F2H_MSG_CONTEXT_BAD 2
#define F2H_MSG_LOG_BLOCK 3
#define MSG_HDR_GET_ID(x) ((x)&0xff)
#define MSG_HDR_GET_SIZE(x) (((x)>>8)&0xff)
#define min_t(type,a,b) ((type)(a)<(type)(b)?(type)(a):(type)(b))
#define dev_err_ratelimited(...) model_log(__VA_ARGS__)
#define IS_ERR(p) ((intptr_t)(p)<0 && (intptr_t)(p)>-4096)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
struct llist_node { struct llist_node *next; };
struct llist_head { _Atomic(struct llist_node *) first; };
struct list_head { int initialized; };
struct waitq { pthread_mutex_t lock; pthread_cond_t cond; atomic_int waits; bool initialized; };
struct kmem_cache { int id; atomic_int live; atomic_int allocs; atomic_int frees; atomic_int tasks; };
struct device { const char *name; };
struct platform_device { struct device dev; };
static void model_log(struct device*d,const char*fmt,...){}
struct task_struct;
struct a6xx_hwsched_hfi { struct kmem_cache *f2h_cache;struct task_struct *f2h_task;u32 irq_mask;int msglock;struct list_head msglist;struct llist_head f2h_msglist,f2h_secondary_list;struct waitq f2h_wq; };
struct a6xx_gmu_device {struct platform_device *pdev;struct {int irq;} hfi;};
struct adreno_device {struct a6xx_hwsched_hfi hfi;struct a6xx_gmu_device gmu;atomic_int processed;int order[8];};
struct task_struct {pthread_t thread;atomic_bool stop;struct adreno_device *dev;int (*fn)(void*);};
static _Thread_local struct task_struct *self;
static int failure, names_live, caches_live, task_calls, irq_calls, joined, destroyed, cache_ids;
static int allocation_fail;
static struct a6xx_hwsched_hfi *to_a6xx_hwsched_hfi(struct adreno_device *a){return &a->hfi;}
static struct a6xx_gmu_device *to_a6xx_gmu(struct adreno_device *a){return &a->gmu;}
static void init_llist_head(struct llist_head *h){atomic_store(&h->first,NULL);}
static bool llist_empty(struct llist_head *h){return atomic_load(&h->first)==NULL;}
static void llist_add(struct llist_node*n,struct llist_head*h){struct llist_node*old=atomic_load(&h->first);do{n->next=old;}while(!atomic_compare_exchange_weak(&h->first,&old,n));}
static struct llist_node *llist_del_all(struct llist_head*h){return atomic_exchange(&h->first,NULL);}
static struct llist_node *llist_reverse_order(struct llist_node *n){struct llist_node *r=NULL;while(n){struct llist_node*t=n->next;n->next=r;r=n;n=t;}return r;}
#define entry(n,type,member) ((type *)((char *)(n)-offsetof(type,member)))
#define llist_for_each_entry_safe(pos,tmp,node,member) \
 for(pos=(node)?entry(node,__typeof__(*pos),member):NULL;pos && ((tmp=pos->member.next?entry(pos->member.next,__typeof__(*pos),member):NULL),1);pos=tmp)
#define rwlock_init(p) (*(p)=1)
#define INIT_LIST_HEAD(p) ((p)->initialized=1)
static void init_waitqueue_head(struct waitq*w){if(w->initialized){pthread_cond_destroy(&w->cond);pthread_mutex_destroy(&w->lock);}w->initialized=true;assert(!pthread_mutex_init(&w->lock,NULL));assert(!pthread_cond_init(&w->cond,NULL));atomic_store(&w->waits,0);}
static bool kthread_should_stop(void){return atomic_load(&self->stop);}
#define wait_event_interruptible(w,condition) do { \
 pthread_mutex_lock(&(w).lock);while(!(condition)){atomic_fetch_add(&(w).waits,1);pthread_cond_wait(&(w).cond,&(w).lock);}pthread_mutex_unlock(&(w).lock); \
}while(0)
static void wake_up_interruptible(struct waitq*w){pthread_mutex_lock(&w->lock);pthread_cond_broadcast(&w->cond);pthread_mutex_unlock(&w->lock);}
static void *task_main(void*p){self=p;self->fn(self->dev);return NULL;}
static struct task_struct *kthread_run(int(*fn)(void*),void*arg,const char*name){struct task_struct*t;task_calls++;if(failure==3)return ERR_PTR(-EAGAIN);t=calloc(1,sizeof(*t));assert(t);t->fn=fn;t->dev=arg;if(strcmp(name,"test")){assert(t->dev->hfi.f2h_cache&&t->dev->hfi.msglist.initialized&&llist_empty(&t->dev->hfi.f2h_secondary_list));}if(t->dev->hfi.f2h_cache)atomic_fetch_add(&t->dev->hfi.f2h_cache->tasks,1);assert(!pthread_create(&t->thread,NULL,task_main,t));return t;}
static int kthread_stop(struct task_struct*t){struct timespec until;atomic_store(&t->stop,true);wake_up_interruptible(&t->dev->hfi.f2h_wq);clock_gettime(CLOCK_REALTIME,&until);until.tv_nsec+=300000000;if(until.tv_nsec>=1000000000){until.tv_sec++;until.tv_nsec-=1000000000;}int ret=pthread_timedjoin_np(t->thread,NULL,&until);if(ret)return -ret;if(t->dev->hfi.f2h_cache)atomic_fetch_sub(&t->dev->hfi.f2h_cache->tasks,1);joined++;free(t);return 0;}
static const char *dev_name(struct device*d){return d->name;}
static char *kasprintf(int flag,const char*fmt,...){char*p=NULL;va_list ap;if(failure==1)return NULL;va_start(ap,fmt);assert(vasprintf(&p,fmt,ap)>=0);va_end(ap);names_live++;return p;}
static void kfree(void*p){if(p){names_live--;free(p);}}
static struct kmem_cache *kmem_cache_create(const char*name,size_t size,size_t align,int flags,void*ctor){struct kmem_cache*c;if(failure==2)return NULL;c=calloc(1,sizeof(*c));assert(c);c->id=++cache_ids;caches_live++;return c;}
#define KMEM_CACHE(type,flags) kmem_cache_create(#type,sizeof(struct type),__alignof__(struct type),flags,NULL)
static void kmem_cache_destroy(struct kmem_cache*c){assert(c);assert(atomic_load(&c->live)==0&&atomic_load(&c->tasks)==0);caches_live--;destroyed++;free(c);}
struct allocation {struct kmem_cache *owner;max_align_t alignment;};
static void *kmem_cache_alloc(struct kmem_cache*c,int flags){struct allocation*p;assert(c);if(allocation_fail)return NULL;p=calloc(1,sizeof(*p)+128);assert(p);p->owner=c;atomic_fetch_add(&c->live,1);atomic_fetch_add(&c->allocs,1);return p+1;}
static void kmem_cache_free(struct kmem_cache*c,void*ptr){struct allocation*p=(struct allocation*)ptr-1;assert(c==p->owner);atomic_fetch_sub(&c->live,1);atomic_fetch_add(&c->frees,1);free(p);}
static void process(struct adreno_device*a,int kind){int n=atomic_load(&a->processed);assert(n<8);a->order[n]=kind;atomic_store(&a->processed,n+1);}
static void process_ts_retire(struct adreno_device*a,u32*p){process(a,F2H_MSG_TS_RETIRE);}
static void process_ctx_bad(struct adreno_device*a,u32*p){process(a,F2H_MSG_CONTEXT_BAD);}
static void process_log_block(struct adreno_device*a,u32*p){process(a,F2H_MSG_LOG_BLOCK);}
static int a6xx_hwsched_hfi_handler(int irq,void*data){return 0;}
static int a6xx_gmu_request_irq(struct adreno_device*a,bool hfi,int(*handler)(int,void*),void*data){irq_calls++;return failure==4?-ENXIO:23;}
#include "actual-hfi-functions.h"
static void close_waitq(struct adreno_device*a){pthread_cond_destroy(&a->hfi.f2h_wq.cond);pthread_mutex_destroy(&a->hfi.f2h_wq.lock);a->hfi.f2h_wq.initialized=false;}
static void fresh(struct adreno_device*a,struct platform_device*p){memset(a,0,sizeof(*a));a->gmu.pdev=p;atomic_store(&a->hfi.f2h_secondary_list.first,(void*)0x1234);}
static void wait_processed(struct adreno_device*a,int expected){for(int i=0;i<3000;i++){if(atomic_load(&a->processed)==expected)return;usleep(1000);}assert(!"worker processing timeout");}
static void cleanup_success(struct adreno_device*a){assert(!kthread_stop(a->hfi.f2h_task));a->hfi.f2h_task=NULL;assert(llist_empty(&a->hfi.f2h_msglist)&&llist_empty(&a->hfi.f2h_secondary_list));kmem_cache_destroy(a->hfi.f2h_cache);a->hfi.f2h_cache=NULL;close_waitq(a);}
int main(void){unsigned cases=0;struct platform_device p={.dev={.name="gmu-A"}},q={.dev={.name="gmu-B"}};struct adreno_device a,b;
 /* Exercise the real loop blocked in its actual wait predicate. */
 fresh(&a,&p);init_llist_head(&a.hfi.f2h_msglist);init_llist_head(&a.hfi.f2h_secondary_list);init_waitqueue_head(&a.hfi.f2h_wq);a.hfi.f2h_task=kthread_run(hfi_f2h_main,&a,"test");
 for(int i=0;i<3000&&!atomic_load(&a.hfi.f2h_wq.waits);i++)usleep(1000);
 assert(atomic_load(&a.hfi.f2h_wq.waits));int stop=kthread_stop(a.hfi.f2h_task);
 if(stop){fprintf(stderr,"FAIL: actual HFI worker did not join after stop wake (%d)\n",stop);return 1;}
 a.hfi.f2h_task=NULL;close_waitq(&a);cases++;
 for(int f=1;f<=4;f++){failure=f;task_calls=irq_calls=joined=destroyed=0;fresh(&a,&p);int ret=a6xx_hwsched_hfi_probe(&a);assert(ret==(f==3?-EAGAIN:f==4?-ENXIO:-ENOMEM));assert(!a.hfi.f2h_task&&!a.hfi.f2h_cache&&!names_live&&!caches_live);assert(task_calls==(f>=3)&&irq_calls==(f==4));assert(joined==(f==4)&&destroyed==(f>=3));assert(llist_empty(&a.hfi.f2h_secondary_list));cases++;failure=0;assert(!a6xx_hwsched_hfi_probe(&a));cleanup_success(&a);cases++;}
 failure=0;fresh(&a,&p);a.hfi.f2h_cache=(void*)0x5678;assert(a6xx_hwsched_hfi_probe(&a)==-EBUSY&&a.hfi.f2h_cache==(void*)0x5678&&atomic_load(&a.hfi.f2h_secondary_list.first)==(void*)0x1234);cases++;fresh(&a,&p);a.hfi.f2h_task=(void*)0x9876;assert(a6xx_hwsched_hfi_probe(&a)==-EBUSY&&a.hfi.f2h_task==(void*)0x9876);cases++;fresh(&a,&p);fresh(&b,&q);assert(!a6xx_hwsched_hfi_probe(&a));assert(!a6xx_hwsched_hfi_probe(&b));assert(a.hfi.f2h_cache&&b.hfi.f2h_cache&&a.hfi.f2h_cache!=b.hfi.f2h_cache);assert(caches_live==2&&!names_live);cases++;
 struct task_struct*task=a.hfi.f2h_task;struct kmem_cache*cache=a.hfi.f2h_cache;int old_irq=irq_calls;assert(a6xx_hwsched_hfi_probe(&a)==-EBUSY);assert(task==a.hfi.f2h_task&&cache==a.hfi.f2h_cache&&irq_calls==old_irq);cases++;
 u32 msg[2]={(2<<8)|F2H_MSG_TS_RETIRE,0},log[2]={(2<<8)|F2H_MSG_LOG_BLOCK,0};
 add_f2h_packet(&a,msg);adreno_a6xx_add_log_block(&a,log);add_f2h_packet(&b,msg);adreno_a6xx_add_log_block(&b,log);wait_processed(&a,2);wait_processed(&b,2);assert(a.order[0]==1&&a.order[1]==3&&b.order[0]==1&&b.order[1]==3);cases+=2;
 allocation_fail=1;add_f2h_packet(&a,msg);adreno_a6xx_add_log_block(&a,log);allocation_fail=0;assert(atomic_load(&a.processed)==2);cases++;
 cleanup_success(&a);assert(caches_live==1);msg[0]=(2<<8)|F2H_MSG_CONTEXT_BAD;add_f2h_packet(&b,msg);wake_up_interruptible(&b.hfi.f2h_wq);wait_processed(&b,3);assert(b.order[2]==2);cleanup_success(&b);assert(!caches_live&&!names_live);cases+=2;
 fresh(&a,&p);assert(!a6xx_hwsched_hfi_probe(&a));cleanup_success(&a);cases++;
 printf("%u HFI worker/probe ownership scenarios passed; actual loop/probe/packet functions, pthread wait/stop, kernel IRQ/slab modeled\n",cases);return 0;}
