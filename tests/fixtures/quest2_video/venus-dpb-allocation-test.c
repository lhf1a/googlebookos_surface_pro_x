/* Execute generated allocator/free bodies; mock only kernel and DMA boundaries. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
typedef uint32_t u32;
#define GFP_KERNEL 0
#define PAGE_SIZE 4096
#define VB2_MAX_FRAME 32
#define DMA_ATTR_WRITE_COMBINE 1
#define DMA_ATTR_NO_KERNEL_MAPPING 2
#define HFI_BUFFER_OUTPUT 2
#define HFI_BUFFER_OUTPUT2 3
#define IS_SQ2_HFI(c) ((c)->sq2)
#define HFI_BUFREQ_COUNT_MIN(r,v) ((void)(v),(r)->count)
struct list_head {struct list_head *next,*prev;};
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
#define INIT_LIST_HEAD(h) ((h)->next=(h),(h)->prev=(h))
static bool list_empty(struct list_head *h){return h->next==h;}
static void list_add_tail(struct list_head *p,struct list_head *h){p->next=h;p->prev=h->prev;h->prev->next=p;h->prev=p;}
static void list_del_init(struct list_head *p){p->prev->next=p->next;p->next->prev=p->prev;INIT_LIST_HEAD(p);}
#define list_for_each_entry_safe(p,n,h,m) for(p=container_of((h)->next,__typeof__(*p),m),n=container_of(p->m.next,__typeof__(*p),m);&p->m!=(h);p=n,n=container_of(n->m.next,__typeof__(*n),m))
enum hfi_version {V4=4};
struct resources {enum hfi_version hfi_version;};
struct device {int unused;};
struct venus_core {struct device *dev;struct resources *res;bool sq2;};
struct retired {int unused;};
enum owner {DRIVER,FIRMWARE};
struct intbuf {struct list_head list;struct retired sq2_retired;u32 type,size,dpb_out_tag;uintptr_t da;void *va;unsigned long attrs;enum owner owned_by;};
struct venus_inst {struct venus_core *core;struct list_head dpbbufs;u32 dpb_buftype,dpb_fmt,output_buf_size,output2_buf_size,sq2_extra_mask,sq2_extra_size;int dpb_ids;bool ended;};
struct hfi_buffer_requirements {u32 count;};
static unsigned allocations,frees,dma_allocations,retired_count;
static bool alloc_fail;
static struct intbuf *retired[8];
static void *kzalloc(size_t n,int flags){(void)flags;allocations++;return alloc_fail?NULL:calloc(1,n);}
static void kfree(void *p){if(p)frees++;free(p);}
static int venus_helper_get_bufreq(struct venus_inst *i,u32 type,struct hfi_buffer_requirements *r){assert(i&&type==HFI_BUFFER_OUTPUT);r->count=2;return 0;}
static void *mock_intbuf_alloc(struct venus_inst *i,struct intbuf *b){assert(i);dma_allocations++;b->da=0x1000+dma_allocations*0x10000;return calloc(1,b->size);}
#define sq2_intbuf_alloc(i,b) ((void)dev,mock_intbuf_alloc(i,b))
static void sq2_intbuf_dma_free(struct venus_inst *i,struct intbuf *b){assert(i);free(b->va);b->va=NULL;}
static bool sq2_venus_buffer_valid(uintptr_t d,u32 size){return d&&size;}
static int ida_alloc_min(int *id,int minimum,int flags){(void)flags;if(*id<minimum)*id=minimum;return (*id)++;}
static void ida_free(int *id,u32 tag){(void)id;(void)tag;}
static bool sq2_venus_scope_ended(struct venus_inst *i,struct retired *r){(void)r;return i->ended;}
static void sq2_intbuf_retire(struct venus_inst *i,struct intbuf *b){assert(b->owned_by!=FIRMWARE||i->ended);assert(retired_count<8);list_del_init(&b->list);retired[retired_count++]=b;}
#include "venus-sq2-extradata.h"
/* ALLOCATOR */
static void finish(struct venus_inst *i){
 i->ended=true;assert(!venus_helper_free_dpb_bufs(i));assert(list_empty(&i->dpbbufs));
 for(unsigned n=0;n<retired_count;n++){sq2_intbuf_dma_free(i,retired[n]);free(retired[n]);}
 retired_count=0;
}
int main(void){
 struct device dev={0};struct resources res={V4};struct venus_core core={&dev,&res,true};
 for(unsigned mode=0;mode<5;mode++){
  struct venus_inst inst={.core=&core,.dpb_buftype=HFI_BUFFER_OUTPUT,.dpb_fmt=1,.output_buf_size=4096,.sq2_extra_mask=1,.sq2_extra_size=188};INIT_LIST_HEAD(&inst.dpbbufs);
  allocations=frees=dma_allocations=retired_count=0;alloc_fail=false;
  struct intbuf *firmware=NULL,*driver=NULL;
  if(mode==1||mode==4){
   firmware=calloc(1,sizeof(*firmware));driver=calloc(1,sizeof(*driver));firmware->owned_by=FIRMWARE;driver->owned_by=DRIVER;
   list_add_tail(&firmware->list,&inst.dpbbufs);list_add_tail(&driver->list,&inst.dpbbufs);
  }
  if(mode<=1)inst.output_buf_size=UINT32_MAX;
  if(mode==2)inst.sq2_extra_mask=0;
  if(mode==4)alloc_fail=true;
  int ret=venus_helper_alloc_dpb_bufs(&inst);
  if(mode<=1){assert(ret==-EOVERFLOW);assert(allocations==1&&frees==1&&!dma_allocations);}
  else if(mode==4){assert(ret==-ENOMEM);assert(allocations==1&&!frees&&!dma_allocations);}
  else{assert(!ret);assert(allocations==2&&!frees&&dma_allocations==2);struct intbuf *b=container_of(inst.dpbbufs.next,struct intbuf,list);assert(b->size==(mode==2?4096:8192));}
  if(firmware){assert(inst.dpbbufs.next==&firmware->list&&firmware->list.next==&inst.dpbbufs);assert(retired_count==1&&retired[0]==driver);assert(firmware->owned_by==FIRMWARE);}
  finish(&inst);
 }
 return 0;
}
