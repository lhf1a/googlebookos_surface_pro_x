/* Actual restore/soft-reset functions; SMMU/GPU hardware calls are modeled. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define IS_ERR_OR_NULL(p) (!(p)||(uintptr_t)(p)>=(uintptr_t)-4095)
#define KGSL_STATE_AWARE 3
#define ADRENO_DEVICE_PWRON 1
#define KGSL_DEVICE(a) ((a)->device)
#define WARN_ON(x) ((void)(x))
#define ADRENO_QUIRK(a,q) false
#define oob_gpu 0
#define oob_boot_slumber 1
#define ADRENO_REG_RBBM_SW_RESET_CMD 0
#define PT_INFO_OFFSET(x) 0
#define FOR_EACH_RINGBUFFER(a,r,i) for ((i)=0; (i)<3 && ((r)=&(a)->rings[i]); (i)++)
#define ADRENO_DEVICE_STARTED 0
#define KGSL_MMU_DEVICE(m) ((m)->device)
#define ADRENO_DEVICE(d) ((d)->adreno)
#define ADRENO_GPU_DEVICE(a) ((a)->gpudev)
#define MMU_OP_VALID(m,op) ((m)->mmu_ops&&(m)->mmu_ops->op)
#define dev_err(...) ((void)0)
struct kgsl_device;
struct kgsl_pagetable { void *priv; };
struct kgsl_iommu_pt { bool attached; uint64_t ttbr0; };
struct kgsl_mmu;
struct kgsl_mmu_ops { int (*mmu_set_pt)(struct kgsl_mmu *,struct kgsl_pagetable *); };
struct kgsl_mmu { struct kgsl_device *device;struct kgsl_pagetable *defaultpagetable;const struct kgsl_mmu_ops *mmu_ops;bool split; };
struct adreno_smmu_priv { const void *cookie;int (*reset_ttbr0)(const void *); };
struct adreno_device;
struct kgsl_device { struct kgsl_mmu mmu;struct adreno_smmu_priv smmu;struct {int transition_error;} pwrctrl;struct adreno_device *adreno;unsigned reset_counter; int state; struct {bool recovered;} *snapshot; };
struct adreno_ringbuffer;
struct kgsl_context {int refs;struct adreno_ringbuffer *rb;};
struct adreno_context {struct kgsl_context base;};
struct adreno_ringbuffer {struct adreno_context *drawctxt_active;unsigned *pagetable_desc;};
struct adreno_gpudev {int (*regulator_enable)(struct adreno_device *);int (*clear_pending_transactions)(struct adreno_device *);void (*start)(struct adreno_device *);int (*rb_start)(struct adreno_device *);};
struct adreno_device {struct kgsl_device *device;const struct adreno_gpudev *gpudev;bool a304;unsigned long priv;struct adreno_ringbuffer rings[3];struct {unsigned gpu_busy,bif_ram_cycles,bif_ram_cycles_read_ch1,bif_ram_cycles_write_ch0,bif_ram_cycles_write_ch1,bif_starved_ram,bif_starved_ram_ch1;} busy_data;};
static int reset_calls,reset_error,power_error,pending_error,rb_error;
static int irq_off,irq_on,gpu_starts,rb_starts,soft_resets,published,coresight,rb_stops,ctx_clears,restore_perf;
static int reg_error,reg_calls,mmio_phase,mmu_error,mmu_calls,oob_error,hfi_error,fsr_error;
static int power_offs,oob_calls,dispatch_starts;
static bool left_on;
static struct adreno_context contexts[3];
static unsigned pt_names[3];
static bool test_bit(int bit,unsigned long *p){return !!(*p&(1UL<<bit));}
static int regulator_enable(struct adreno_device *a){assert(mmio_phase==3);reg_calls++;return reg_error;}
static void adreno_writereg(struct adreno_device *a,int reg,unsigned value){assert(reg==0);if(mmio_phase==0){assert(value==1);mmio_phase=1;}else{assert(mmio_phase==2&&!value);mmio_phase=3;soft_resets++;}}
static void adreno_readreg(struct adreno_device *a,int reg,unsigned *value){assert(reg==0&&mmio_phase==1);mmio_phase=2;*value=1;}
static void kgsl_context_put(struct kgsl_context *c){assert(!c->rb->drawctxt_active&&!*c->rb->pagetable_desc);assert(c->refs==1);c->refs--;ctx_clears++;}
static void kgsl_sharedmem_writel(unsigned *p,int offset,unsigned value){assert(!offset&&!value);*p=value;}
static bool regulators_left_on(struct kgsl_device *d){return left_on;}
static int kgsl_mmu_start(struct kgsl_device *d){mmu_calls++;return mmu_error;}
static void adreno_get_bus_counters(struct adreno_device *a){}
static void adreno_clear_dcvs_counters(struct adreno_device *a){}
static void adreno_perfcounter_start(struct adreno_device *a){}
static int kgsl_mmu_clear_fsr(struct kgsl_mmu *m){return fsr_error;}
static void adreno_dispatcher_start(struct kgsl_device *d){dispatch_starts++;}
static int a6xx_gmu_oob_set(struct kgsl_device *d,int n){oob_calls++;return oob_error;}
static int a6xx_rgmu_oob_set(struct kgsl_device *d,int n){return a6xx_gmu_oob_set(d,n);}
static void a6xx_gmu_oob_clear(struct kgsl_device *d,int n){}
static void a6xx_rgmu_oob_clear(struct kgsl_device *d,int n){}
static void gmu_core_dev_oob_clear(struct kgsl_device *d,int n){}
static int a6xx_gmu_hfi_start_msg(struct adreno_device *a){return hfi_error;}
static int a6xx_gmu_power_off(struct adreno_device *a){power_offs++;return 0;}
static void a6xx_rgmu_power_off(struct adreno_device *a){power_offs++;}
static bool kgsl_iommu_split_tables_enabled(struct kgsl_mmu *m){return m->split;}
static bool adreno_is_a304(struct adreno_device *a){return a->a304;}
static int clear_pending(struct adreno_device *a){return pending_error;}
static int restore_root(const void *cookie){assert(cookie==(void *)0x1234);reset_calls++;return reset_error;}
static int kgsl_pwrctrl_change_state(struct kgsl_device *d,int state){return state==KGSL_STATE_AWARE?power_error:0;}
static void adreno_irqctrl(struct adreno_device *a,int on){if(on)irq_on++;else irq_off++;}
static void adreno_clear_gpu_fault(struct adreno_device *a){}
static void clear_bit(int bit,unsigned long *p){*p&=~(1UL<<bit);}
static void set_bit(int bit,unsigned long *p){*p|=1UL<<bit;}
static void adreno_perfcounter_save(struct adreno_device *a){}
static void adreno_perfcounter_restore(struct adreno_device *a){restore_perf++;}
static void adreno_ringbuffer_set_global(struct adreno_device *a,int id){assert(((reset_calls==1&&!reset_error)||(mmu_calls==1&&!mmu_error))&&!id&&ctx_clears==3);published++;}
static void gpu_start(struct adreno_device *a){assert(published==1);gpu_starts++;}
static void adreno_coresight_start(struct adreno_device *a){coresight++;}
static void adreno_ringbuffer_stop(struct adreno_device *a){rb_stops++;}
static int rb_start(struct adreno_device *a){rb_starts++;return rb_error;}
static void a6xx_start(struct adreno_device *a){gpu_start(a);}
static int a6xx_rb_start(struct adreno_device *a){return rb_start(a);}
static void a6xx_enable_gpu_irq(struct adreno_device *a){adreno_irqctrl(a,1);}
static void a6xx_disable_gpu_irq(struct adreno_device *a){adreno_irqctrl(a,0);}
#include "default-root-functions.h"
static const struct kgsl_mmu_ops mmu_ops={kgsl_iommu_set_pt};
static const struct adreno_gpudev gpu_ops={regulator_enable,clear_pending,gpu_start,rb_start};
static struct kgsl_device gpu;
static struct adreno_device adreno;
static struct kgsl_pagetable parent,child;
static struct kgsl_iommu_pt parent_private;
static void setup(void){
 memset(&gpu,0,sizeof(gpu));memset(&adreno,0,sizeof(adreno));
 parent_private=(struct kgsl_iommu_pt){true,0x123000};parent.priv=&parent_private;child.priv=&parent_private;
 gpu.mmu=(struct kgsl_mmu){&gpu,&parent,&mmu_ops,true};gpu.smmu=(struct adreno_smmu_priv){(void *)0x1234,restore_root};gpu.adreno=&adreno;
 adreno.device=&gpu;adreno.gpudev=&gpu_ops;adreno.priv=1;
 reset_calls=reset_error=power_error=pending_error=rb_error=0;
 reg_error=reg_calls=mmio_phase=mmu_error=mmu_calls=oob_error=hfi_error=fsr_error=0;
 power_offs=oob_calls=dispatch_starts=0;left_on=false;
 for(int i=0;i<3;i++){pt_names[i]=17+i;contexts[i].base=(struct kgsl_context){1,&adreno.rings[i]};adreno.rings[i]=(struct adreno_ringbuffer){&contexts[i],&pt_names[i]};}
 irq_off=irq_on=gpu_starts=rb_starts=soft_resets=published=coresight=rb_stops=ctx_clears=restore_perf=0;
}
static void contexts_held(void){assert(!ctx_clears);for(int i=0;i<3;i++)assert(contexts[i].base.refs==1&&adreno.rings[i].drawctxt_active==&contexts[i]&&pt_names[i]==17U+i);}
static void not_restarted(void){assert(!published&&!gpu_starts&&!irq_on&&!coresight&&!rb_starts&&!rb_stops&&!gpu.reset_counter);}
int main(void){int cases=0;
 setup();assert(!kgsl_mmu_set_pt(&gpu.mmu,&parent)&&reset_calls==1);cases++;
 setup();assert(kgsl_mmu_set_pt(&gpu.mmu,&child)==-EINVAL&&!reset_calls);cases++;
 setup();assert(kgsl_mmu_set_pt(&gpu.mmu,NULL)==-EINVAL&&!reset_calls);cases++;
 setup();parent.priv=NULL;assert(kgsl_mmu_set_pt(&gpu.mmu,&parent)==-EINVAL&&!reset_calls);cases++;
 setup();parent_private.attached=false;assert(kgsl_mmu_set_pt(&gpu.mmu,&parent)==-ENODEV&&!reset_calls);cases++;
 setup();parent_private.ttbr0=0;assert(kgsl_mmu_set_pt(&gpu.mmu,&parent)==-ENODEV&&!reset_calls);cases++;
 setup();gpu.mmu.split=false;assert(kgsl_mmu_set_pt(&gpu.mmu,&parent)==-ENODEV&&!reset_calls);cases++;
 setup();gpu.smmu.cookie=NULL;assert(kgsl_mmu_set_pt(&gpu.mmu,&parent)==-ENODEV&&!reset_calls);cases++;
 setup();gpu.smmu.reset_ttbr0=NULL;assert(kgsl_mmu_set_pt(&gpu.mmu,&parent)==-ENODEV&&!reset_calls);cases++;
 setup();gpu.mmu.mmu_ops=NULL;assert(kgsl_mmu_set_pt(&gpu.mmu,&parent)==-ENODEV&&!reset_calls);cases++;
 setup();reset_error=-EIO;assert(kgsl_mmu_set_pt(&gpu.mmu,&parent)==-EIO&&gpu.pwrctrl.transition_error==-EIO&&reset_calls==1);cases++;
 setup();reset_error=-ETIMEDOUT;gpu.pwrctrl.transition_error=-EIO;assert(kgsl_mmu_set_pt(&gpu.mmu,&parent)==-ETIMEDOUT&&gpu.pwrctrl.transition_error==-EIO);cases++;
 setup();assert(!adreno_soft_reset(&gpu)&&soft_resets==1&&irq_off==1&&published==1&&gpu_starts==1&&irq_on==1&&rb_starts==1&&gpu.reset_counter==1&&adreno.priv==1&&restore_perf==1);cases++;
 setup();reset_error=-ETIMEDOUT;assert(adreno_soft_reset(&gpu)==-ETIMEDOUT&&reset_calls==1&&irq_off==1&&!adreno.priv&&gpu.pwrctrl.transition_error==-ETIMEDOUT);not_restarted();contexts_held();cases++;
 setup();power_error=-EIO;assert(adreno_soft_reset(&gpu)==-EIO&&!ctx_clears&&!irq_off&&!soft_resets&&!reset_calls&&adreno.priv==1);not_restarted();cases++;
 setup();pending_error=-EBUSY;assert(adreno_soft_reset(&gpu)==-EBUSY&&!soft_resets&&!reset_calls);not_restarted();cases++;
 setup();adreno.a304=true;assert(adreno_soft_reset(&gpu)==-ENODEV&&!reset_calls);not_restarted();cases++;
 setup();rb_error=-ENOSPC;assert(adreno_soft_reset(&gpu)==-ENOSPC&&!gpu.reset_counter&&!adreno.priv&&restore_perf==1);cases++;
 setup();reg_error=-ETIMEDOUT;assert(adreno_soft_reset(&gpu)==-ETIMEDOUT&&reg_calls==1&&soft_resets==1&&!reset_calls);not_restarted();contexts_held();cases++;
 setup();struct adreno_gpudev no_reg=gpu_ops;no_reg.regulator_enable=NULL;adreno.gpudev=&no_reg;assert(!adreno_soft_reset(&gpu)&&!reg_calls&&ctx_clears==3);cases++;
 setup();adreno.rings[1].drawctxt_active=NULL;adreno_set_active_ctxs_null(&adreno);assert(ctx_clears==2&&!pt_names[1]);adreno_set_active_ctxs_null(&adreno);assert(ctx_clears==2);cases++;
 setup();adreno.priv=0;assert(!_adreno_start(&adreno)&&ctx_clears==3&&mmu_calls==1&&!soft_resets&&dispatch_starts==1);cases++;
 setup();adreno.priv=0;mmu_error=-EIO;assert(_adreno_start(&adreno)==-EIO&&!published&&!gpu_starts);contexts_held();cases++;
 setup();adreno.priv=0;left_on=true;reg_error=-ETIMEDOUT;assert(_adreno_start(&adreno)==-ETIMEDOUT&&!mmu_calls&&!published&&!gpu_starts);contexts_held();cases++;
 setup();adreno.priv=0;left_on=true;assert(!_adreno_start(&adreno)&&reg_calls==1&&mmu_calls==1&&ctx_clears==3);cases++;
 setup();adreno.priv=0;power_error=-EIO;assert(_adreno_start(&adreno)==-EIO&&!mmu_calls&&!published);contexts_held();cases++;
 int (*boots[])(struct adreno_device *)={gmu_gpu_boot,rgmu_gpu_boot};
 for(unsigned i=0;i<2;i++){
  setup();assert(!boots[i](&adreno)&&ctx_clears==3&&mmu_calls==1&&published==1&&dispatch_starts==1&&!power_offs);cases++;
  setup();mmu_error=-ETIMEDOUT;assert(boots[i](&adreno)==-ETIMEDOUT&&!published&&!oob_calls&&!gpu_starts&&power_offs==1);contexts_held();cases++;
  setup();oob_error=-EIO;assert(boots[i](&adreno)==-EIO&&ctx_clears==3&&!gpu_starts&&power_offs==1);cases++;
  setup();fsr_error=-EIO;assert(boots[i](&adreno)==-EIO&&ctx_clears==3&&!rb_starts&&power_offs==1);cases++;
  setup();rb_error=-ENOSPC;assert(boots[i](&adreno)==-ENOSPC&&!dispatch_starts&&!gpu.reset_counter&&power_offs==1&&irq_off==1);cases++;
 }
 printf("%d default-root/reset/context/boot scenarios passed; hardware, power and parent initialization modeled\n",cases);
 return 0;
}
