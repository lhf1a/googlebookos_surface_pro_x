/* Actual record structures/reset functions; hardware and provider leaves modeled. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#undef errno
typedef uint64_t u64;
typedef uint32_t u32;
#include "hard-reset-records.h"
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define IS_ERR_OR_NULL(p) (!(p)||(uintptr_t)(p)>=(uintptr_t)-4095)
#define IS_ALIGNED(v,a) (((v)&((a)-1))==0)
static bool warn_on(bool x){assert(!x);return x;}
#define WARN_ON(x) warn_on(x)
#define ADRENO_APRIV 1
#define ADRENO_PREEMPT_NONE 0
#define GMU_PRIV_GPU_STARTED 0
#define A6XX_CP_RB_CNTL_DEFAULT 0x1234U
#define A6XX_CP_CTXRECORD_MAGIC_REF 0xAE399D6EUL
#define A6XX_CP_SMMU_INFO_MAGIC_REF 0x241350D5UL
#define PREEMPT_RECORD(f) offsetof(struct a6xx_cp_preemption_record,f)
#define PREEMPT_SMMU_RECORD(f) offsetof(struct a6xx_cp_smmu_info,f)
#define PT_INFO_OFFSET(f) offsetof(struct adreno_ringbuffer_pagetable_info,f)
struct kgsl_memdesc {void *hostptr;u64 size,gpuaddr;};
struct kgsl_pagetable {int name;u64 ttbr0;u32 contextidr;};
struct kgsl_mmu {bool secured;struct kgsl_pagetable *defaultpagetable;};
struct kgsl_iommu {struct kgsl_memdesc *smmu_info;};
struct adreno_device;
struct kgsl_device {struct {int transition_error;} pwrctrl;struct kgsl_mmu mmu;struct kgsl_iommu iommu;struct adreno_device *adreno;};
struct adreno_ringbuffer {struct kgsl_memdesc *pagetable_desc,*preemption_desc,*buffer_desc;int id,preempt_lock;u32 wptr,_wptr;bool skip_inline_wptr;};
struct adreno_preemption {struct kgsl_memdesc *scratch;int timer,state;};
struct a6xx_gmu_device {int power_error;unsigned long flags;};
struct adreno_device {struct kgsl_device dev;struct a6xx_gmu_device gmu;struct adreno_preemption preempt;struct adreno_ringbuffer ringbuffers[2],*next_rb,*prev_rb,*cur_rb;unsigned num_ringbuffers;bool preemption,apriv;};
#define KGSL_DEVICE(a) (&(a)->dev)
#define ADRENO_DEVICE(d) ((d)->adreno)
#define KGSL_IOMMU_PRIV(d) (&(d)->iommu)
#define ADRENO_FEATURE(a,f) ((a)->apriv)
#define MMU_DEFAULT_TTBR0(d) ((d)->mmu.defaultpagetable->ttbr0)
#define MMU_DEFAULT_CONTEXTIDR(d) ((d)->mmu.defaultpagetable->contextidr)
#define FOR_EACH_RINGBUFFER(a,r,i) for((i)=0;(i)<(a)->num_ringbuffers&&((r)=&(a)->ringbuffers[i]);(i)++)
#define SCRATCH_RPTR_GPU_ADDR(d,id) (0x900000ULL+(id)*4)
static u64 kgsl_mmu_pagetable_get_ttbr0(struct kgsl_pagetable *p){return p->ttbr0;}
static u32 kgsl_mmu_pagetable_get_contextidr(struct kgsl_pagetable *p){return p->contextidr;}
static bool adreno_is_preemption_enabled(struct adreno_device *a){return a->preemption;}
static int barriers,timer_stops,hard_stops,root_restores,retired,active_puts,boots,irq_off,hfi_stop;
static int stop_error,root_error,boot_error;
static bool stopped,root_ready;
static void wmb(void){barriers++;}
#define spin_lock_irqsave(p,f) do{(f)=0;assert(!*(p));*(p)=1;}while(0)
#define spin_unlock_irqrestore(p,f) do{(void)(f);assert(*(p));*(p)=0;}while(0)
static void timer_delete_sync(int *p){timer_stops++;*p=0;}
static void adreno_set_preempt_state(struct adreno_device *a,int state){a->preempt.state=state;}
static struct a6xx_gmu_device *to_a6xx_gmu(struct adreno_device *a){return &a->gmu;}
static void clear_bit(unsigned n,unsigned long *p){*p&=~(1UL<<n);}
static void a6xx_hfi_stop(struct adreno_device *a){hfi_stop++;}
static void a6xx_disable_gpu_irq(struct adreno_device *a){irq_off++;}
static int a6xx_gmu_suspend(struct adreno_device *a){assert(hfi_stop&&irq_off);hard_stops++;if(!stop_error)stopped=true;return stop_error;}
static int kgsl_mmu_set_pt(struct kgsl_mmu *m,struct kgsl_pagetable *p){assert(stopped&&p==m->defaultpagetable);root_restores++;if(!root_error)root_ready=true;return root_error;}
static void records_clean(struct adreno_device *a);
static void adreno_drawctxt_retire_switches(struct adreno_device *a){assert(stopped&&root_ready);records_clean(a);retired++;}
static void adreno_set_active_ctxs_null(struct adreno_device *a){assert(retired==1);active_puts++;}
static int a6xx_boot(struct adreno_device *a){assert(retired==1&&active_puts==1&&!a->gmu.flags);boots++;return boot_error;}
#include "hard-reset-functions.h"
static struct adreno_device gpu;
static struct kgsl_pagetable parent;
static struct kgsl_memdesc pt[2],pre[2],buf[2],smmu,scratch;
static u64 pt_data[2][16],pre_data[2][32],smmu_data[16],scratch_data[16];
static void setup(void){memset(&gpu,0,sizeof(gpu));gpu.dev.adreno=&gpu;parent=(struct kgsl_pagetable){0,0xabc000,0};gpu.dev.mmu.defaultpagetable=&parent;gpu.dev.iommu.smmu_info=&smmu;gpu.preempt.scratch=&scratch;gpu.preempt.state=4;gpu.preempt.timer=1;gpu.num_ringbuffers=2;gpu.preemption=true;gpu.gmu.flags=1;gpu.next_rb=&gpu.ringbuffers[1];gpu.prev_rb=&gpu.ringbuffers[1];gpu.cur_rb=&gpu.ringbuffers[1];for(int i=0;i<2;i++){memset(pt_data[i],0xa5,sizeof(pt_data[i]));memset(pre_data[i],0xa5,sizeof(pre_data[i]));pt[i]=(struct kgsl_memdesc){pt_data[i],sizeof(pt_data[i]),0x10000+i*4096};pre[i]=(struct kgsl_memdesc){pre_data[i],sizeof(pre_data[i]),0x20000+i*4096};buf[i]=(struct kgsl_memdesc){NULL,32768,0x30000+i*32768};gpu.ringbuffers[i]=(struct adreno_ringbuffer){&pt[i],&pre[i],&buf[i],i,0,17,19,true};}memset(smmu_data,0xa5,sizeof(smmu_data));memset(scratch_data,0xa5,sizeof(scratch_data));smmu=(struct kgsl_memdesc){smmu_data,sizeof(smmu_data),0x50000};scratch=(struct kgsl_memdesc){scratch_data,sizeof(scratch_data),0x60000};barriers=timer_stops=hard_stops=root_restores=retired=active_puts=boots=irq_off=hfi_stop=0;stop_error=root_error=boot_error=0;stopped=root_ready=false;}
static bool zero_bytes(const void *p,size_t n){const unsigned char *c=p;for(size_t i=0;i<n;i++)if(c[i])return false;return true;}
static void records_clean(struct adreno_device *a){assert(!a->next_rb&&!a->prev_rb&&a->cur_rb==&a->ringbuffers[0]&&barriers>0);for(int i=0;i<2;i++){struct adreno_ringbuffer *r=&a->ringbuffers[i];struct adreno_ringbuffer_pagetable_info *t=pt[i].hostptr;assert(t->current_rb_ptname==parent.name&&t->ttbr0==parent.ttbr0&&t->contextidr==parent.contextidr&&!t->incoming_ptname&&!t->switch_pt_enable);assert(!r->wptr&&!r->_wptr&&!r->skip_inline_wptr);if(a->preemption){struct a6xx_cp_preemption_record *c=pre[i].hostptr;assert(c->magic==A6XX_CP_CTXRECORD_MAGIC_REF&&!c->info&&!c->errno&&!c->data&&!c->rptr&&!c->wptr&&!c->counter);assert(c->cntl==(A6XX_CP_RB_CNTL_DEFAULT|(a->apriv?0:1U<<27)));assert(c->rptr_addr==SCRATCH_RPTR_GPU_ADDR(&a->dev,i)&&c->rbase==buf[i].gpuaddr);assert(zero_bytes((char *)c+sizeof(*c),pre[i].size-sizeof(*c)));}}if(a->preemption){struct a6xx_cp_smmu_info *s=smmu.hostptr;assert(s->ttbr0==parent.ttbr0&&s->context_idr==parent.contextidr&&s->asid==0xDECAFBAD&&s->magic==A6XX_CP_SMMU_INFO_MAGIC_REF);assert(zero_bytes(scratch_data,sizeof(scratch_data))&&!a->preempt.state&&timer_stops==1);}}
static void unchanged(void){assert(!barriers&&!timer_stops&&!retired&&!active_puts&&!boots);assert(pt_data[0][0]==0xa5a5a5a5a5a5a5a5ULL&&pre_data[0][0]==0xa5a5a5a5a5a5a5a5ULL&&scratch_data[0]==0xa5a5a5a5a5a5a5a5ULL);}
int main(void){unsigned cases=0;
 setup();assert(!a6xx_gmu_restart(&gpu.dev)&&hard_stops==1&&root_restores==1&&retired==1&&active_puts==1&&boots==1);records_clean(&gpu);cases++;
 setup();gpu.apriv=true;assert(!a6xx_gmu_restart(&gpu.dev));records_clean(&gpu);cases++;
 setup();gpu.preemption=false;gpu.dev.iommu.smmu_info=NULL;gpu.preempt.scratch=NULL;gpu.ringbuffers[0].preemption_desc=NULL;assert(!a6xx_gmu_restart(&gpu.dev)&&!timer_stops);records_clean(&gpu);cases++;
 setup();gpu.gmu.power_error=-EIO;assert(a6xx_gmu_restart(&gpu.dev)==-EIO&&!hard_stops&&!irq_off&&!hfi_stop);unchanged();cases++;
 setup();stop_error=-ETIMEDOUT;assert(a6xx_gmu_restart(&gpu.dev)==-ETIMEDOUT&&!root_restores&&gpu.gmu.flags==1);unchanged();cases++;
 setup();root_error=-EIO;assert(a6xx_gmu_restart(&gpu.dev)==-EIO&&hard_stops==1&&root_restores==1&&!gpu.gmu.flags);unchanged();cases++;
 setup();gpu.dev.mmu.secured=true;assert(a6xx_gmu_restart(&gpu.dev)==-EOPNOTSUPP&&root_restores==1);unchanged();cases++;
 setup();boot_error=-EIO;assert(a6xx_gmu_restart(&gpu.dev)==-EIO&&retired==1&&boots==1);records_clean(&gpu);cases++;
 setup();gpu.dev.mmu.defaultpagetable=NULL;assert(a6xx_preemption_reset_records(&gpu)==-ENODEV);unchanged();cases++;
 setup();parent.ttbr0=0;assert(a6xx_preemption_reset_records(&gpu)==-ENODEV);unchanged();cases++;
 setup();gpu.num_ringbuffers=0;assert(a6xx_preemption_reset_records(&gpu)==-EINVAL);unchanged();cases++;
 setup();gpu.ringbuffers[1].pagetable_desc=NULL;assert(a6xx_gmu_restart(&gpu.dev)==-EINVAL);unchanged();cases++;
 setup();pt[1].hostptr=NULL;assert(a6xx_gmu_restart(&gpu.dev)==-EINVAL);unchanged();cases++;
 setup();pt[1].size=sizeof(struct adreno_ringbuffer_pagetable_info)-1;assert(a6xx_gmu_restart(&gpu.dev)==-EINVAL);unchanged();cases++;
 setup();pre[1].hostptr=NULL;assert(a6xx_gmu_restart(&gpu.dev)==-EINVAL);unchanged();cases++;
 setup();pre[1].size=sizeof(struct a6xx_cp_preemption_record)-1;assert(a6xx_gmu_restart(&gpu.dev)==-EINVAL);unchanged();cases++;
 setup();gpu.ringbuffers[1].buffer_desc=NULL;assert(a6xx_gmu_restart(&gpu.dev)==-EINVAL);unchanged();cases++;
 setup();gpu.dev.iommu.smmu_info=NULL;assert(a6xx_gmu_restart(&gpu.dev)==-EINVAL);unchanged();cases++;
 setup();smmu.size=sizeof(struct a6xx_cp_smmu_info)-1;assert(a6xx_gmu_restart(&gpu.dev)==-EINVAL);unchanged();cases++;
 setup();scratch.size=sizeof(u64);assert(a6xx_gmu_restart(&gpu.dev)==-EINVAL);unchanged();cases++;
 setup();scratch.hostptr=NULL;assert(a6xx_gmu_restart(&gpu.dev)==-EINVAL);unchanged();cases++;
 printf("%u hard-reset/restore-record/restart scenarios passed; hardware/provider/ref release modeled\n",cases);return 0;
}
