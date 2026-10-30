/* SPDX-License-Identifier: GPL-2.0-only */
/* Actual allocator wrapper and retire queue, mock DMA/VB2/module providers. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
typedef atomic_int refcount_t;
#define refcount_set(p,n) atomic_store(p,n)
#define refcount_inc(p) ((void)atomic_fetch_add(p,1))
#define refcount_dec_and_test(p) (atomic_fetch_sub(p,1)==1)
#define SQ2_VENUS_RETIRE_HOST_TEST
#define SQ2_VENUS_VB2_HOST_TEST
#define EXPORT_SYMBOL_GPL(x)
#define GFP_KERNEL 0
#define THIS_MODULE NULL
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define IS_ERR_OR_NULL(p) (!(p) || IS_ERR(p))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
struct list_head {struct list_head *next,*prev;};
#define INIT_LIST_HEAD(p) ((p)->next=(p)->prev=(p))
#define LIST_HEAD(n) struct list_head n={&n,&n}
static void list_add_tail(struct list_head *p,struct list_head *h)
{p->prev=h->prev;p->next=h;h->prev->next=p;h->prev=p;}
static void list_del_init(struct list_head *p)
{p->prev->next=p->next;p->next->prev=p->prev;INIT_LIST_HEAD(p);}
static void list_splice_init(struct list_head *h,struct list_head *d)
{if(h->next!=h){d->next=h->next;d->prev=h->prev;d->next->prev=d;d->prev->next=d;INIT_LIST_HEAD(h);}}
#define list_for_each_entry_safe(e,n,h,m) \
 for(e=container_of((h)->next,__typeof__(*e),m),n=container_of(e->m.next,__typeof__(*n),m); \
 &e->m!=(h);e=n,n=container_of(n->m.next,__typeof__(*n),m))
static void mutex_init(pthread_mutex_t *p){assert(!pthread_mutex_init(p,NULL));}
static void mutex_lock(pthread_mutex_t *p){assert(!pthread_mutex_lock(p));}
static void mutex_unlock(pthread_mutex_t *p){assert(!pthread_mutex_unlock(p));}
struct device;
struct venus_core {pthread_mutex_t sq2_retire_lock;struct list_head sq2_retired;bool sq2_dma_stopped;struct device *dev;unsigned long sq2_vb2_bytes;};
struct venus_inst {struct venus_core *core;struct sq2_venus_scope *sq2_scope;struct list_head dpbbufs,internalbufs;};
struct vb2_queue {struct venus_inst *priv;};
struct vb2_buffer {struct vb2_queue *vb2_queue;};
static void *vb2_get_drv_priv(struct vb2_queue *q){return q->priv;}
struct device {int refs;};struct vm_area_struct {int unused;};
struct dma_buf {int refs;unsigned long size;};
static int pins, allocations, maps, unmaps, detaches, put_count, fail_alloc, fail_attach, fail_map;
static bool try_module_get(void *p){(void)p;++pins;return true;}
static void module_put(void *p){(void)p;assert(pins>0);--pins;}
static void *kzalloc(size_t n,int flags){(void)flags;return calloc(1,n);}
#define kfree free
#define WARN_ON(x) ((x) ? (assert(!(x)),1) : 0)
static void get_dma_buf(struct dma_buf *d){assert(d->refs>0);d->refs++;}
static void dma_buf_put(struct dma_buf *d){assert(d->refs>0);d->refs--;}
struct backing {bool imported,mapped;};
static void *alloc_mem(struct vb2_buffer *v,struct device *d,unsigned long n)
{(void)v;(void)d;(void)n;if(fail_alloc)return ERR_PTR(-ENOMEM);allocations++;return calloc(1,sizeof(struct backing));}
static void put_mem(void *p){put_count++;allocations--;free(p);}
static void *attach_mem(struct vb2_buffer *v,struct device *d,struct dma_buf *b,unsigned long n)
{(void)b;if(fail_attach)return ERR_PTR(-EIO);struct backing *p=alloc_mem(v,d,n);if(!IS_ERR(p))p->imported=true;return p;}
static void detach_mem(void *p){struct backing *b=p;assert(b->imported&&!b->mapped);detaches++;allocations--;free(p);}
static int map_mem(void *p){struct backing *b=p;assert(!b->mapped);if(fail_map)return -EIO;b->mapped=true;maps++;return 0;}
static void unmap_mem(void *p){struct backing *b=p;assert(b->mapped);b->mapped=false;unmaps++;}
static void sync_mem(void *p){assert(p);}
static void *address_mem(struct vb2_buffer *v,void *p){(void)v;return p;}
static unsigned int users_mem(void *p){assert(p);return 1;}
static int mmap_mem(void *p,struct vm_area_struct *v){(void)v;assert(p);return 0;}
static struct dma_buf *export_mem(struct vb2_buffer *v,void *p,unsigned long f)
{(void)v;(void)p;(void)f;return ERR_PTR(-ENOSYS);}
struct vb2_mem_ops {
 void *(*alloc)(struct vb2_buffer *,struct device *,unsigned long);void (*put)(void *);
 struct dma_buf *(*get_dmabuf)(struct vb2_buffer *,void *,unsigned long);
 void *(*attach_dmabuf)(struct vb2_buffer *,struct device *,struct dma_buf *,unsigned long);
 void (*detach_dmabuf)(void *);int (*map_dmabuf)(void *);void (*unmap_dmabuf)(void *);
 void (*prepare)(void *);void (*finish)(void *);
 void *(*cookie)(struct vb2_buffer *,void *);void *(*vaddr)(struct vb2_buffer *,void *);
 unsigned int (*num_users)(void *);int (*mmap)(void *,struct vm_area_struct *);
};
static const struct vb2_mem_ops vb2_dma_contig_memops={
 .alloc=alloc_mem,.put=put_mem,.get_dmabuf=export_mem,.attach_dmabuf=attach_mem,
 .detach_dmabuf=detach_mem,.map_dmabuf=map_mem,.unmap_dmabuf=unmap_mem,
 .prepare=sync_mem,.finish=sync_mem,.cookie=address_mem,.vaddr=address_mem,
 .num_users=users_mem,.mmap=mmap_mem,
};
#include "../probe/video/sq2-venus-retire.c"
#include "../probe/video/venus-sq2-vb2.h"
struct intbuf {
 struct list_head list;
 struct sq2_venus_retired sq2_retired;
 struct device *sq2_dev;
 struct venus_core *sq2_core;
 unsigned long sq2_charged;
 size_t size;void *va;unsigned long da,attrs;
};
static int internal_freed;
static int internal_alloc_calls, internal_alloc_fail;
static void *dma_alloc_attrs(struct device *d,size_t n,unsigned long *a,int g,unsigned long f)
{(void)d;(void)g;(void)f;internal_alloc_calls++;if(internal_alloc_fail)return NULL;*a=0x30000000;return calloc(1,n);}
static struct device *get_device(struct device *d){d->refs++;return d;}
static void put_device(struct device *d){assert(d->refs>0);d->refs--;}
static void dma_free_attrs(struct device *d,size_t n,void *v,unsigned long a,unsigned long f)
{assert(d&&n==4096);(void)a;(void)f;free(v);internal_freed++;}
#define IS_SQ2_HFI(c) ((void)(c),true)
#include "../probe/video/venus-sq2-intbuf.h"
static void internal_test(bool transfer)
{
 struct device dev={0};struct venus_core core={.dev=&dev};
 struct venus_inst *inst=calloc(1,sizeof(*inst));assert(inst);inst->core=&core;
 struct intbuf *saved[5];
 sq2_venus_retire_init(&core);INIT_LIST_HEAD(&inst->dpbbufs);INIT_LIST_HEAD(&inst->internalbufs);
 internal_freed=0;
 for(int i=0;i<5;i++) {
  struct intbuf *b=calloc(1,sizeof(*b));assert(b);saved[i]=b;
  b->size=4096;b->va=calloc(1,b->size);assert(b->va);
  list_add_tail(&b->list,i<2?&inst->dpbbufs:&inst->internalbufs);
 }
 if(transfer) venus_helper_sq2_retire_buffers(inst);
 printf("close residual buffers: core_owned=%d device_refs=%d freed=%d\n",transfer?5:0,dev.refs,internal_freed);
 free(inst); /* callback must not touch the instance or its IDA */
 sq2_venus_retire_stopped(&core);
 if(transfer) assert(internal_freed==5&&!dev.refs);
 else for(int i=0;i<5;i++){free(saved[i]->va);free(saved[i]);} /* harness-only leak cleanup */
 assert(!pthread_mutex_destroy(&core.sq2_retire_lock));
}
static void internal_budget_test(void)
{
 struct device dev={0};struct venus_core core={.dev=&dev};struct venus_inst inst={.core=&core};
 struct intbuf *b=calloc(1,sizeof(*b)),*other=calloc(1,sizeof(*other));assert(b&&other);
 b->size=other->size=4096;sq2_venus_retire_init(&core);
 assert(!sq2_venus_vb2_reserve(&core,SQ2_VENUS_VB2_BUDGET-4096));
 b->va=sq2_intbuf_alloc(&inst,b);assert(b->va&&b->sq2_charged==4096);
 int calls=internal_alloc_calls;
 assert(!sq2_intbuf_alloc(&inst,other)&&internal_alloc_calls==calls&&!other->sq2_charged);
 sq2_venus_vb2_unreserve(&core,SQ2_VENUS_VB2_BUDGET-4096);
 internal_alloc_fail=1;assert(!sq2_intbuf_alloc(&inst,other));internal_alloc_fail=0;
 assert(core.sq2_vb2_bytes==4096&&!other->sq2_charged);
 other->va=sq2_intbuf_alloc(&inst,other);assert(other->va&&core.sq2_vb2_bytes==8192);
 sq2_intbuf_dma_free(&inst,other);assert(core.sq2_vb2_bytes==4096);free(other);
 sq2_intbuf_retire(&inst,b);assert(core.sq2_vb2_bytes==4096&&dev.refs==1);
 sq2_venus_retire_stopped(&core);assert(!core.sq2_vb2_bytes&&!dev.refs);
 assert(!pthread_mutex_destroy(&core.sq2_retire_lock));
 printf("INTERNAL_BUDGET_PASS: shared exact limit, no provider call over budget, provider failure/direct-free refunds, retirement holds charge until actual release\n");
}
static atomic_int released;
static void race_release(struct sq2_venus_retired *entry)
{atomic_fetch_add(&released,1);free(entry);}
static void *producer(void *arg)
{
 for(int i=0;i<1000;i++) {
  struct sq2_venus_retired *e=calloc(1,sizeof(*e));assert(e);
  INIT_LIST_HEAD(&e->list);e->release=race_release;sq2_venus_retire(arg,e);
 }
 return NULL;
}
static void *stopper(void *arg)
{for(int i=0;i<1000;i++)sq2_venus_retire_stopped(arg);return NULL;}
static void scope_test(void)
{
 struct venus_core core={0};struct venus_inst a={.core=&core},b={.core=&core};
 struct vb2_queue q={&a};struct vb2_buffer vb={&q};
 const struct vb2_mem_ops *ops=&sq2_vb2_memops;
 sq2_venus_retire_init(&core);
 assert(!sq2_venus_scope_begin(&a)&&!sq2_venus_scope_begin(&b));
 assert(sq2_venus_scope_begin(&a)==-EIO); /* cannot replace unconfirmed scope */
 void *first=ops->alloc(&vb,NULL,4096),*reused=ops->alloc(&vb,NULL,4096);
 void *late=ops->alloc(&vb,NULL,4096),*failed=ops->alloc(&vb,NULL,4096);
 assert(!IS_ERR(first)&&!IS_ERR(reused)&&!IS_ERR(late)&&!IS_ERR(failed));
 assert(!sq2_venus_scope_bind(&a,first)&&!sq2_venus_scope_bind(&a,reused));
 assert(!sq2_venus_scope_bind(&a,late)&&!sq2_venus_scope_bind(&b,failed));
 assert(!sq2_venus_scope_ended(&a,reused));
 assert(sq2_venus_scope_bind(&b,reused)==-EIO); /* old scope still live */
 ops->put(first);ops->put(failed);assert(core.sq2_vb2_bytes==16384);
 sq2_venus_scope_end(&a);assert(core.sq2_vb2_bytes==12288); /* only retired A */
 assert(sq2_venus_scope_ended(&a,reused));
 assert(!sq2_venus_scope_begin(&a));assert(!sq2_venus_scope_bind(&a,reused));
 assert(!sq2_venus_scope_ended(&a,reused)&&sq2_venus_scope_ended(&a,late));
 ops->put(late);assert(core.sq2_vb2_bytes==8192); /* old-generation late retire */
 ops->put(reused);assert(core.sq2_vb2_bytes==8192); /* new generation remains */
 sq2_venus_scope_end(&a);assert(core.sq2_vb2_bytes==4096);
 sq2_venus_scope_drop(&a);sq2_venus_scope_drop(&b); /* failed B survives owner */
 sq2_venus_retire_stopped(&core);assert(!core.sq2_vb2_bytes&&!pins&&!allocations);
 assert(!pthread_mutex_destroy(&core.sq2_retire_lock));
 printf("SCOPE_PASS: only ended generation reclaimed; late old retire freed; reused object rebound and held for new END; failed other session held until PAS stop\n");
}
int main(int argc,char **argv)
{
 struct venus_core core={0};struct venus_inst inst={.core=&core};struct vb2_queue q={&inst};
 struct vb2_buffer vb={&q};struct dma_buf db={.refs=1,.size=8192};void *p;
 const struct vb2_mem_ops *ops=&sq2_vb2_memops;
 if(argc>1 && argv[1][0]=='i') {internal_test(argv[1][1]=='1');return 0;}
 sq2_venus_retire_init(&core);
 if(argc>1) {
  ops=argv[1][0]=='s' ? &vb2_dma_contig_memops : &sq2_vb2_memops;
  p=ops->attach_dmabuf(&vb,NULL,&db,4096);assert(!IS_ERR(p));
  assert(!ops->map_dmabuf(p));ops->unmap_dmabuf(p);ops->detach_dmabuf(p);dma_buf_put(&db);
  printf("VB2 close before PAS confirmation: refs=%d mappings=%d unmaps=%d detach=%d pins=%d\n",db.refs,maps-unmaps,unmaps,detaches,pins);
  sq2_venus_retire_stopped(&core);assert(!allocations&&!pins&&!db.refs);
  assert(!pthread_mutex_destroy(&core.sq2_retire_lock));return 0;
 }
 fail_alloc=1;assert(IS_ERR(ops->alloc(&vb,NULL,4096)));assert(!pins);fail_alloc=0;
 fail_attach=1;assert(IS_ERR(ops->attach_dmabuf(&vb,NULL,&db,4096)));assert(db.refs==1&&!pins);fail_attach=0;
 assert(!core.sq2_vb2_bytes);
 assert(sq2_venus_vb2_reserve(&core,0)==-ENOSPC);
 assert(sq2_venus_vb2_reserve(&core,~0UL)==-ENOSPC);
 assert(!sq2_venus_vb2_reserve(&core,SQ2_VENUS_VB2_BUDGET));
 assert(ops->alloc(&vb,NULL,1)==ERR_PTR(-ENOSPC)&&!allocations&&!pins);
 sq2_venus_vb2_unreserve(&core,SQ2_VENUS_VB2_BUDGET);
 p=ops->attach_dmabuf(&vb,NULL,&db,4096);assert(!IS_ERR(p)&&db.refs==2&&pins==1);
 fail_map=1;assert(ops->map_dmabuf(p)==-EIO);fail_map=0;
 assert(!ops->map_dmabuf(p));ops->unmap_dmabuf(p);assert(!unmaps);
 assert(!ops->map_dmabuf(p)&&maps==1); /* same retained mapping */
 ops->detach_dmabuf(p);dma_buf_put(&db); /* VB2 reference goes away */
 assert(db.refs==1&&allocations==1&&pins==1&&!unmaps&&!detaches);
 assert(core.sq2_vb2_bytes==8192); /* full exported size, not requested4096 */
 printf("before PAS confirmation: refs=%d mappings=%d unmaps=%d detach=%d pins=%d\n",db.refs,maps-unmaps,unmaps,detaches,pins);
 p=ops->alloc(&vb,NULL,4096);assert(!IS_ERR(p));ops->put(p);assert(!put_count&&allocations==2);
 void *late=ops->alloc(&vb,NULL,4096);assert(!IS_ERR(late));
 db.refs++;void *unmapped=ops->attach_dmabuf(&vb,NULL,&db,4096);assert(!IS_ERR(unmapped));
 ops->detach_dmabuf(unmapped);dma_buf_put(&db);
 assert(core.sq2_vb2_bytes==24576);
 sq2_venus_retire_stopped(&core);
 assert(!db.refs&&allocations==1&&pins==1&&unmaps==1&&detaches==2&&put_count==1);
 assert(core.sq2_vb2_bytes==4096);
 sq2_venus_retire_stopped(&core);assert(unmaps==1); /* no double release */
 assert(ops->alloc(&vb,NULL,4096)==ERR_PTR(-EIO));
 ops->put(late);assert(!allocations&&!pins&&put_count==2&&!core.sq2_vb2_bytes);
 printf("VB2 budget: zero/overflow/full rejected, full exporter charged, failed allocation refunded, retired bytes held, post-stop allocation refused\n");
 printf("confirmed stop: refs=%d outstanding_allocations=%d pins=%d; immediate late retirement OK\n",db.refs,allocations,pins);
 assert(!pthread_mutex_destroy(&core.sq2_retire_lock));
 sq2_venus_retire_init(&core);
 pthread_t a,b,s;
 assert(!pthread_create(&a,NULL,producer,&core));
 assert(!pthread_create(&b,NULL,producer,&core));
 assert(!pthread_create(&s,NULL,stopper,&core));
 assert(!pthread_join(a,NULL)&&!pthread_join(b,NULL)&&!pthread_join(s,NULL));
 sq2_venus_retire_stopped(&core);assert(atomic_load(&released)==2000);
 assert(!pthread_mutex_destroy(&core.sq2_retire_lock));
 printf("retire/stop race: 2000 entries released exactly once; not whole-driver concurrency proof\n");
 internal_test(true);
 internal_budget_test();
 scope_test();
 return 0;
}
