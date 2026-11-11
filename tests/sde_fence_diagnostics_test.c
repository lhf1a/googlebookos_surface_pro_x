/* SPDX-License-Identifier: GPL-2.0-only */
/* Actual source and current ops declaration; kernel scheduler/refcount leaves modeled. */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
typedef unsigned long long u64;
typedef long long ktime_t;
typedef int spinlock_t;
struct list_head {struct list_head *next,*prev;};
struct kref {int refs;};
struct dma_fence;
#include "actual-ops.h"
struct dma_fence {const struct dma_fence_ops *ops;u64 seqno;};
#define SDE_FENCE_NAME_SIZE 24
#include "actual-context.h"
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
#define READ_ONCE(x) (*(volatile __typeof__(x) *)&(x))
static char logbuf[1024];
#define SDE_ERROR(...) snprintf(logbuf,sizeof(logbuf),__VA_ARGS__)
#define SDE_DEBUG(...) ((void)0)
struct seq_file {char text[1024];};
#define seq_printf(s,...) snprintf((s)->text,sizeof((s)->text),__VA_ARGS__)
#define kfree free
static void kref_put(struct kref *r,void (*release)(struct kref*)){if(!--r->refs)release(r);}
static int already,wait_calls;static long wait_result,last_timeout;
static bool last_intr;
static struct dma_fence *sync_file_get_fence(int fd){return NULL;}
static void dma_fence_put(struct dma_fence *f){}
static bool dma_fence_is_signaled(struct dma_fence *f){return already;}
static long msecs_to_jiffies(long ms){return ms;}
static long dma_fence_wait_timeout(struct dma_fence *f,bool intr,long timeout){wait_calls++;last_intr=intr;last_timeout=timeout;return wait_result;}
static long dma_fence_default_wait(struct dma_fence *f,bool intr,long timeout){return wait_result;}
#include "actual-source.h"
static int cases;
#define CHECK(x) do{cases++;if(!(x)){fprintf(stderr,"FAIL line%d %s\n",__LINE__,#x);exit(1);}}while(0)
static const char *foreign_name(struct dma_fence *f){return "sde_fence:foreign";}
static const char *foreign_timeline(struct dma_fence *f){return "foreign-context";}
static const struct dma_fence_ops foreign_ops={.get_driver_name=foreign_name,.get_timeline_name=foreign_timeline};
int main(void)
{
 struct sde_fence_context ctx={.name="sde-context"};
 struct sde_fence own={.base={.ops=&sde_fence_ops,.seqno=99},.ctx=&ctx,.name="own-fence"};
 /* Foreign allocation contains only the base; an unchecked private cast is ASan-visible. */
 struct dma_fence *foreign=malloc(sizeof(*foreign));CHECK(foreign);
 *foreign=(struct dma_fence){.ops=&foreign_ops,.seqno=0x100000003ULL};
 char out[128],want[32];unsigned int values[]={0,17,INT_MAX,UINT_MAX};
 for(unsigned int i=0;i<sizeof(values)/sizeof(values[0]);i++){
  ctx.done_count=values[i];memset(out,0xa5,sizeof(out));
  sde_fence_timeline_value_str(&own.base,out,sizeof(out));snprintf(want,sizeof(want),"%u",values[i]);
  CHECK(!strcmp(out,want));CHECK(ctx.done_count==values[i]&&own.base.seqno==99);
 }
 memset(out,0xa5,sizeof(out));sde_fence_timeline_value_str(foreign,out,sizeof(out));CHECK(!strcmp(out,"unavailable"));
 CHECK(foreign->seqno==0x100000003ULL);
 sde_fence_timeline_value_str(NULL,out,sizeof(out));CHECK(!strcmp(out,"unavailable"));
 own.ctx=NULL;sde_fence_timeline_value_str(&own.base,out,sizeof(out));CHECK(!strcmp(out,"unavailable"));own.ctx=&ctx;
 out[0]='X';sde_fence_timeline_value_str(foreign,out,0);CHECK(out[0]=='X');
 sde_fence_timeline_value_str(foreign,out,-1);CHECK(out[0]=='X');
 sde_fence_timeline_value_str(foreign,NULL,128);
 sde_fence_timeline_value_str(foreign,out,1);CHECK(out[0]==0);
 memset(out,0xa5,sizeof(out));sde_fence_timeline_value_str(foreign,out,4);CHECK(!strcmp(out,"una")&&(unsigned char)out[4]==0xa5);
 ctx.done_count=42;struct seq_file seq={0};struct seq_file *sp=&seq;
 sde_fence_list_dump(&own.base,&sp);CHECK(strstr(seq.text,"timeline:42 ")&&strstr(seq.text,"seqno:0x63 "));
 sde_fence_list_dump(foreign,&sp);CHECK(strstr(seq.text,"timeline:unavailable ")&&strstr(seq.text,"seqno:0x100000003 ")&&strstr(seq.text,"signaled:0xffffffff"));
 long results[]={0,-EINVAL,-512,9};
 for(int which=0;which<2;which++)for(unsigned int i=0;i<sizeof(results)/sizeof(results[0]);i++){
  struct dma_fence *f=which?foreign:&own.base;wait_calls=already=0;wait_result=results[i];logbuf[0]=0;
  CHECK(sde_sync_wait(f,53)==results[i]);CHECK(wait_calls==1&&last_timeout==53&&last_intr);
  if(results[i]==0||results[i]==-EINVAL)CHECK(strstr(logbuf,which?"timeline:unavailable ":"timeline:42 "));
  else CHECK(logbuf[0]==0);
 }
 already=1;wait_calls=0;CHECK(sde_sync_wait(foreign,53)==53&&wait_calls==0);
 CHECK(sde_sync_wait(foreign,0)==1&&wait_calls==0);CHECK(sde_sync_wait(NULL,53)==-EINVAL);
 CHECK(sde_fence_ops.release==sde_fence_release&&sde_fence_ops.wait==dma_fence_default_wait&&sde_fence_ops.signaled==sde_fence_signaled&&sde_fence_ops.enable_signaling==sde_fence_enable_signaling);
 free(foreign);printf("PASS %d fence diagnostic checks; wait/signaling leaves modeled, foreign base-only allocation checked\n",cases);return 0;
}
