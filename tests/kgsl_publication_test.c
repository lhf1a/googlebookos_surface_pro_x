// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
typedef uint64_t u64;
#define __must_check
#define smp_load_acquire(p) __atomic_load_n((p),__ATOMIC_ACQUIRE)
#define smp_store_release(p,v) __atomic_store_n((p),(v),__ATOMIC_RELEASE)
#define IS_ALIGNED(x,a) (!((x)&((a)-1)))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define KGSL_MEM_ENTRY_KERNEL 0
#define KGSL_DRAWOBJ_PROFILING 1
#define GFP_KERNEL 0
#define pr_context(dev,ctx,...) ((void)(dev))
struct list_head {struct list_head *next,*prev;};
static void INIT_LIST_HEAD(struct list_head *h){h->next=h->prev=h;}
static void list_add_tail(struct list_head *n,struct list_head *h){n->prev=h->prev;n->next=h;h->prev->next=n;h->prev=n;}
static void list_del_init(struct list_head *n){n->prev->next=n->next;n->next->prev=n->prev;INIT_LIST_HEAD(n);}
#define list_for_each_entry_safe(pos,tmp,head,member) for(pos=container_of((head)->next,__typeof__(*pos),member),tmp=container_of(pos->member.next,__typeof__(*pos),member); &pos->member!=(head);pos=tmp,tmp=container_of(tmp->member.next,__typeof__(*tmp),member))
struct kref{int refs;};
struct kgsl_pagetable;
struct kgsl_mmu {struct kgsl_pagetable *securepagetable;};
struct kgsl_pagetable {struct kgsl_mmu *mmu;};
struct kgsl_mem_entry;
struct idr {struct kgsl_mem_entry *entry;};
struct kgsl_process_private {pthread_mutex_t mem_lock;struct idr mem_idr;struct kgsl_pagetable *pagetable;int refs;};
struct kgsl_memdesc {u64 gpuaddr,size,flags;struct kgsl_pagetable *pagetable;pthread_mutex_t lock;bool mapped;int cookie;};
struct kgsl_mem_entry {u64 published_gpuaddr;struct kref refcount;struct kgsl_memdesc memdesc;struct kgsl_process_private *priv;unsigned int id;int pending_free,release_error;bool retained;};
struct kgsl_device {int marker;};
struct kgsl_device_private {struct kgsl_device *device;struct kgsl_process_private *process_priv;};
struct kgsl_context {unsigned int id;};
struct kgsl_memobj_node {struct kgsl_mem_entry *entry;struct list_head node;unsigned int id;u64 offset,gpuaddr,size;unsigned long flags,priv;};
struct kgsl_drawobj {unsigned int flags;};
struct kgsl_drawobj_cmd {struct kgsl_drawobj base;struct kgsl_mem_entry *profiling_buf_entry;struct list_head cmdlist,memlist;};
#define CMDOBJ(p) container_of(p,struct kgsl_drawobj_cmd,base)
struct kgsl_command_object {u64 offset,gpuaddr,size;unsigned int flags,id;};
struct kgsl_gpumem_get_info {unsigned long gpuaddr;unsigned int id,flags;size_t size,mmapsize;unsigned long useraddr;};
struct kgsl_gpuobj_info {unsigned int id;u64 gpuaddr,flags,size,va_len,va_addr;};
static struct kgsl_mmu mmu;
static struct kgsl_pagetable pt={&mmu};
static struct kgsl_process_private process;
static struct kgsl_mem_entry entry;
static struct kgsl_device device;
static struct kgsl_device_private devpriv={&device,&process};
static struct kgsl_context context={9};
static struct {struct {long mapped;} stats;} kgsl_driver;
static int memobjs_cache;
static bool initialized,block_map,entered,release_map,deferred;
static pthread_mutex_t stage_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t stage_cond=PTHREAD_COND_INITIALIZER;
static int map_error,unmap_error,region_error,unmaps,maps,retains,freed,stats_sub,process_puts,gets,entry_puts,deferred_puts,nodes_freed;
static unsigned int cases;
static void spin_lock(pthread_mutex_t *l){assert(!pthread_mutex_lock(l));}
static void spin_unlock(pthread_mutex_t *l){assert(!pthread_mutex_unlock(l));}
#define idr_for_each_entry(tree,e,id) for((id)=0;(id)<1;(id)++) if(((e)=(tree)->entry))
static struct kgsl_mem_entry *idr_find(struct idr *idr,unsigned int id){return id==7?idr->entry:NULL;}
static void idr_remove(struct idr *idr,unsigned int id){assert(id==7);idr->entry=NULL;}
static int kgsl_mem_entry_get(struct kgsl_mem_entry *e){int old=__atomic_fetch_add(&e->refcount.refs,1,__ATOMIC_RELAXED);assert(old>0);gets++;return 1;}
static void kgsl_mem_entry_destroy(struct kref *k);
static void kgsl_mem_entry_put(struct kgsl_mem_entry *e){if(e){assert(e==&entry && e->refcount.refs>0);entry_puts++;if(--e->refcount.refs==0)kgsl_mem_entry_destroy(&e->refcount);}}
static void kgsl_mem_entry_put_deferred(struct kgsl_mem_entry *e){if(e){assert(e==&entry && e->refcount.refs>0);deferred_puts++;if(--e->refcount.refs==0){assert(!deferred);deferred=true;}}}
static int kgsl_mmu_set_svm_region(struct kgsl_pagetable *p,u64 addr,u64 len){assert(p==&pt && addr==0x10000 && len==4096);return region_error;}
static int kgsl_mmu_map(struct kgsl_pagetable *p,struct kgsl_memdesc *m){assert(p==&pt && m==&entry.memdesc && !entry.published_gpuaddr);maps++;pthread_mutex_lock(&stage_lock);entered=true;pthread_cond_broadcast(&stage_cond);while(block_map && !release_map)pthread_cond_wait(&stage_cond,&stage_lock);pthread_mutex_unlock(&stage_lock);if(map_error)return map_error;m->mapped=true;m->cookie=12345;return 0;}
static int kgsl_mmu_put_gpuaddr(struct kgsl_memdesc *m){assert(!smp_load_acquire(&entry.published_gpuaddr));unmaps++;if(unmap_error)return unmap_error;m->mapped=false;m->gpuaddr=0;m->pagetable=NULL;return 0;}
static void kgsl_mem_entry_retain(struct kgsl_mem_entry *e,int error){assert(e==&entry && !e->published_gpuaddr);e->retained=true;e->release_error=error;retains++;}
static void kgsl_process_private_put(struct kgsl_process_private *p){assert(p==&process && p->refs>0);p->refs--;process_puts++;}
static void kgsl_memfree_purge(struct kgsl_pagetable *p,u64 addr,u64 size){}
static unsigned int kgsl_memdesc_usermem_type(struct kgsl_memdesc *m){return 1;}
static int kgsl_sharedmem_free(struct kgsl_memdesc *m){assert(!m->mapped);return 0;}
static void kgsl_process_sub_stats(struct kgsl_process_private *p,unsigned int type,u64 size){stats_sub++;}
static void atomic_long_sub(u64 size,long *v){*v-=size;}
static void kfree(void *p){assert(p==&entry);freed++;}
static bool kgsl_mmu_gpuaddr_in_range(struct kgsl_pagetable *p,u64 addr,u64 size){return p && addr>=0x10000 && addr<0x1000000 && size<=0x1000000-addr;}
static u64 kgsl_memdesc_footprint(struct kgsl_memdesc *m){return m->size;}
static void *kmem_cache_zalloc(int cache,int flags){return calloc(1,sizeof(struct kgsl_memobj_node));}
static void kmem_cache_free(int cache,void *p){nodes_freed++;free(p);}
#include "kgsl-publication-functions.h"
static void reset(void){if(initialized){assert(!pthread_mutex_destroy(&process.mem_lock));assert(!pthread_mutex_destroy(&entry.memdesc.lock));}memset(&entry,0,sizeof(entry));memset(&process,0,sizeof(process));pthread_mutex_init(&process.mem_lock,NULL);pthread_mutex_init(&entry.memdesc.lock,NULL);initialized=true;entry.memdesc.size=4096;entry.priv=&process;entry.refcount.refs=1;entry.id=7;process.refs=1;process.pagetable=&pt;process.mem_idr.entry=&entry;mmu.securepagetable=NULL;kgsl_driver.stats.mapped=4096;map_error=unmap_error=region_error=unmaps=maps=retains=freed=stats_sub=process_puts=gets=entry_puts=deferred_puts=nodes_freed=0;block_map=entered=release_map=deferred=false;cases++;}
static void *map_thread(void *arg){return (void *)(uintptr_t)_gpu_set_svm_region(&process,&entry,0x10000,4096);}
static struct kgsl_memobj_node *add_ib(struct kgsl_drawobj_cmd *cmd,u64 addr,u64 size){struct kgsl_command_object obj={.gpuaddr=addr,.size=size};assert(!kgsl_drawobj_add_memobject(&cmd->cmdlist,&obj));struct kgsl_memobj_node *ib=container_of(cmd->cmdlist.prev,struct kgsl_memobj_node,node);assert(!ib->entry);return ib;}
static void init_cmd(struct kgsl_drawobj_cmd *cmd){memset(cmd,0,sizeof(*cmd));INIT_LIST_HEAD(&cmd->cmdlist);INIT_LIST_HEAD(&cmd->memlist);}
static void finish_deferred(void){assert(deferred && !entry.refcount.refs);deferred=false;kgsl_mem_entry_destroy(&entry.refcount);}
static void assert_info(u64 expected){struct kgsl_gpuobj_info modern={.id=7};struct kgsl_gpumem_get_info legacy={.id=7};assert(!kgsl_ioctl_gpuobj_info(&devpriv,0,&modern));assert(!kgsl_ioctl_gpumem_get_info(&devpriv,0,&legacy));assert(modern.gpuaddr==expected && legacy.gpuaddr==expected);}
int main(void){struct kgsl_drawobj_cmd cmd;struct kgsl_memobj_node *ib;struct kgsl_mem_entry *got;
 for(int failure=0;failure<3;failure++){
  reset();init_cmd(&cmd);ib=add_ib(&cmd,0x10000,64);block_map=true;map_error=failure?-EIO:0;unmap_error=failure==2?-EUCLEAN:0;pthread_t thread;assert(!pthread_create(&thread,NULL,map_thread,NULL));pthread_mutex_lock(&stage_lock);while(!entered)pthread_cond_wait(&stage_cond,&stage_lock);pthread_mutex_unlock(&stage_lock);
  assert(entry.memdesc.gpuaddr==0x10000 && !kgsl_mem_entry_gpuaddr(&entry));assert(!kgsl_sharedmem_find(&process,0x10000));assert_info(0);assert(!verify_dispatch_ib(&devpriv,&context,ib));assert(!verify_hwsched_ib(&devpriv,&context,ib));assert(!ib->entry && entry.refcount.refs==1);
  pthread_mutex_lock(&stage_lock);release_map=true;pthread_cond_broadcast(&stage_cond);pthread_mutex_unlock(&stage_lock);void *ret;pthread_join(thread,&ret);
  if(!failure){assert((uintptr_t)ret==0x10000 && kgsl_mem_entry_gpuaddr(&entry)==0x10000 && entry.memdesc.cookie==12345);assert_info(0x10000);assert(verify_dispatch_ib(&devpriv,&context,ib) && ib->entry==&entry && entry.refcount.refs==2);assert(verify_hwsched_ib(&devpriv,&context,ib) && entry.refcount.refs==2);entry.pending_free=1;kgsl_mem_entry_put(&entry);assert(!freed && entry.refcount.refs==1 && entry.memdesc.mapped);cmdobj_destroy(&cmd.base);assert(deferred && !freed && entry.memdesc.mapped);finish_deferred();assert(freed==1 && unmaps==1 && !entry.published_gpuaddr);}
  else{assert((intptr_t)ret==-EIO && !entry.published_gpuaddr && !kgsl_sharedmem_find(&process,0x10000));assert(!verify_dispatch_ib(&devpriv,&context,ib));if(failure==2)assert(entry.retained && !entry.id && !process.mem_idr.entry && entry.memdesc.gpuaddr==0x10000);else assert(!entry.memdesc.gpuaddr && !entry.retained);cmdobj_destroy(&cmd.base);}
 }
 reset();assert(!kgsl_sharedmem_find(NULL,0x10000));assert(!kgsl_sharedmem_find(&process,0));assert_info(0);
 reset();entry.memdesc.gpuaddr=0x10000;assert(!kgsl_sharedmem_find(&process,0x10000));assert_info(0);
 reset();assert(_gpu_set_svm_region(&process,&entry,0x10000,4096)==0x10000);entry.pending_free=1;assert(!kgsl_sharedmem_find(&process,0x10000));assert(!kgsl_sharedmem_find_id(&process,7));
 reset();assert(_gpu_set_svm_region(&process,&entry,0x10000,4096)==0x10000);assert(!kgsl_sharedmem_find(&process,0x11000));got=kgsl_sharedmem_find(&process,0x10fff);assert(got==&entry);kgsl_mem_entry_put(got);
 for(int validator=0;validator<2;validator++)for(int invalid=0;invalid<5;invalid++){
  reset();assert(_gpu_set_svm_region(&process,&entry,0x10000,4096)==0x10000);init_cmd(&cmd);ib=add_ib(&cmd,0x10000,64);if(invalid==0)ib->size=0;if(invalid==1)ib->size=3;if(invalid==2)ib->gpuaddr++;if(invalid==3)ib->size=4096+4;if(invalid==4)ib->size=(0x100000UL<<2);bool ok=validator?verify_hwsched_ib(&devpriv,&context,ib):verify_dispatch_ib(&devpriv,&context,ib);assert(!ok && !ib->entry && entry.refcount.refs==1);cmdobj_destroy(&cmd.base);
 }
 reset();assert(_gpu_set_svm_region(&process,&entry,0x10000,4096)==0x10000);init_cmd(&cmd);ib=add_ib(&cmd,0x10000,64);assert(verify_dispatch_ib(&devpriv,&context,ib));ib=add_ib(&cmd,0x10ff0,32);assert(!verify_dispatch_ib(&devpriv,&context,ib) && entry.refcount.refs==2);cmdobj_destroy(&cmd.base);assert(entry.refcount.refs==1 && nodes_freed==2 && !freed);
 reset();assert(_gpu_set_svm_region(&process,&entry,0x10000,4096)==0x10000);init_cmd(&cmd);ib=add_ib(&cmd,0x10000,64);assert(verify_dispatch_ib(&devpriv,&context,ib));ib=add_ib(&cmd,0x10040,64);assert(verify_hwsched_ib(&devpriv,&context,ib));assert(entry.refcount.refs==3);kgsl_mem_entry_put(&entry);cmdobj_destroy(&cmd.base);assert(deferred && !freed && deferred_puts==2);finish_deferred();assert(freed==1);
 reset();assert(_gpu_set_svm_region(&process,&entry,0x10000,4096)==0x10000);unmap_error=-EUCLEAN;kgsl_mem_entry_put(&entry);assert(entry.retained && !entry.published_gpuaddr && !process.mem_idr.entry && !freed);
 printf("%u publication/IB scenarios passed, including 3 pthread-blocked map interleavings\n",cases);return 0;
}
