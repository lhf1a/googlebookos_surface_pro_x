/* Actual provider and KGSL helpers; MMIO, PM, IRQ callback and validation mocked. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
struct device;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int irqreturn_t;
#define IRQ_NONE 0
#define IRQ_HANDLED 1
#define IOMMU_FAULT_READ 0
#define IOMMU_FAULT_WRITE 1
#define BIT(n) (1UL << (n))
#define GENMASK(h,l) ((~0UL << (l)) & (~0UL >> (63-(h))))
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define DEFINE_RATELIMIT_STATE(name,...) int name
#define __ratelimit(p) ((void)(p), true)
#include "fault-constants.h"
struct arm_smmu_device { void *dev; };
struct qcom_smmu { struct arm_smmu_device smmu; u32 stall_enabled, fault_hold_enabled, fault_masked, fault_pending; };
struct iommu_domain { int unused; };
struct arm_smmu_domain { struct arm_smmu_device *smmu; struct { u32 cbndx; } cfg; int cb_lock; struct iommu_domain domain; };
static struct qcom_smmu qs;
static struct arm_smmu_domain fixture;
static int refs, locked, pm_error, bank_error, writes, clears, resumes, reports, report_ret, nested_recovery, stuck, cases;
static u32 fsr, sctlr, last_resume;
static struct qcom_smmu *to_qcom_smmu(struct arm_smmu_device *s) { assert(s==&qs.smmu);return &qs; }
static int qcom_adreno_smmu_get_context_bank(const void *c,u32 *bank,u32 *asid) { assert(c==&fixture); if(bank_error)return bank_error;*bank=fixture.cfg.cbndx;*asid=5;return 0; }
static int pm_runtime_resume_and_get(void *dev) { assert(dev==qs.smmu.dev&&!locked);if(pm_error)return pm_error;refs++;return 0; }
static void pm_runtime_get_noresume(void *dev) { assert(dev==qs.smmu.dev&&refs&&locked);refs++; }
static void pm_runtime_put_autosuspend(void *dev) { assert(dev==qs.smmu.dev&&refs>0&&!locked);refs--; }
#define spin_lock_irqsave(p,f) do {assert((p)==&fixture.cb_lock&&!locked&&refs);(f)=0;locked=1;} while(0)
#define spin_unlock_irqrestore(p,f) do {assert((p)==&fixture.cb_lock&&locked&&(f)==0);locked=0;} while(0)
static u32 arm_smmu_cb_read(struct arm_smmu_device *s,u32 bank,u32 reg) { assert(s==&qs.smmu&&bank==fixture.cfg.cbndx&&locked&&refs);if(reg==ARM_SMMU_CB_FSR)return fsr;assert(reg==ARM_SMMU_CB_SCTLR);return sctlr; }
static void arm_smmu_cb_write(struct arm_smmu_device *s,u32 bank,u32 reg,u32 val) {
 assert(s==&qs.smmu&&bank==fixture.cfg.cbndx&&locked&&refs);writes++;
 if(reg==ARM_SMMU_CB_SCTLR)sctlr=val;
 else if(reg==ARM_SMMU_CB_FSR){fsr&=~(val&~ARM_SMMU_CB_FSR_SS);clears++;}
 else {assert(reg==ARM_SMMU_CB_RESUME);last_resume=val;resumes++;if(!stuck)fsr&=~ARM_SMMU_CB_FSR_SS;}
}
static void wmb(void) { assert(locked&&refs); }
#define read_poll_timeout_atomic(op,val,cond,delay,timeout,sleep,...) ({int rc=-ETIMEDOUT;for(int n=0;n<3;n++){val=op(__VA_ARGS__);if(cond){rc=0;break;}}rc;})
static void arm_smmu_read_context_fault_info(struct arm_smmu_device *s,u32 bank,struct arm_smmu_context_fault_info *c) { assert(s==&qs.smmu&&bank==fixture.cfg.cbndx&&locked&&refs);*c=(struct arm_smmu_context_fault_info){.iova=0x12345000,.fsr=fsr,.fsynr=ARM_SMMU_CB_FSYNR0_WNR}; }
static void arm_smmu_print_context_fault_info(struct arm_smmu_device *s,u32 bank,const struct arm_smmu_context_fault_info *c) { assert(refs); }
static int qcom_adreno_smmu_terminate_fault(const void *cookie);
static int qcom_adreno_smmu_rearm_fault(const void *cookie);
static int report_iommu_fault(struct iommu_domain *d,void *dev,unsigned long addr,int flags);

#include "fault-provider.h"
#define KGSL_FT_PAGEFAULT_GPUHALT_ENABLE 1
struct kgsl_iommu_pt { bool attached; };
struct kgsl_mmu;
struct kgsl_pagetable { struct kgsl_iommu_pt *priv; struct kgsl_mmu *mmu; unsigned name; };
struct kgsl_iommu_context { struct kgsl_pagetable *default_pt; bool stalled_on_fault; struct kgsl_device *kgsldev; const char *name; };
struct kgsl_iommu { struct kgsl_iommu_context user_context; };
struct adreno_smmu_fault_info { u64 far,ttbr0;u32 contextidr,fsr,fsynr0,fsynr1,cbfrsynra; };
struct adreno_smmu_priv { const void *cookie; int (*set_fault_policy)(const void *,bool);int (*terminate_fault)(const void *);int (*rearm_fault)(const void *);void (*get_fault_info)(const void *,struct adreno_smmu_fault_info *); };
struct kgsl_device;
struct kgsl_mmu { struct kgsl_device *device; unsigned long features; struct kgsl_iommu iommu; struct kgsl_pagetable *defaultpagetable; };
struct kgsl_device { int mutex; struct adreno_smmu_priv smmu;struct {int transition_error;}pwrctrl;struct kgsl_mmu mmu; };
struct adreno_device { struct kgsl_device dev;unsigned long ft_pf_policy; };
#define _IOMMU_PRIV(m) (&(m)->iommu)
#define KGSL_MMU_DEVICE(m) ((m)->device)
#define ADRENO_DEVICE(d) ((struct adreno_device *)(d))
#define IS_ERR_OR_NULL(p) (!(p)||(uintptr_t)(p)>=(uintptr_t)-4095)
static bool test_bit(unsigned n,const unsigned long *p){return !!(*p&BIT(n));}
#include "fault-consumer.h"
static struct adreno_device gpu;
static bool use_kgsl_callback, mutex_busy;
static int scheduled, client_fault, private_puts, freed_checks;
#define KGSL_IOMMU_FSR_TF ARM_SMMU_CB_FSR_TF
#define KGSL_IOMMU_FSR_PF ARM_SMMU_CB_FSR_PF
#define KGSL_IOMMU_FSR_EF ARM_SMMU_CB_FSR_EF
#define KGSL_IOMMU_FSR_SS_SHIFT 30
#define KGSL_MMU_GLOBAL_PAGETABLE 2
#define KGSL_MMU_GLOBAL_PT 0
#define KGSL_FT_PAGEFAULT_LOG_ONE_PER_PAGE 3
#define KGSL_PWRFLAGS_OFF 0
#define KGSL_STATE_AWARE 1
#define ADRENO_IOMMU_PAGE_FAULT 16
#define ADRENO_GPU_DEVICE(a) (&test_gpudev)
static void logmsg(const char *fmt,...) {}
#define dev_err(d,...) logmsg(__VA_ARGS__)
#define dev_crit(d,...) logmsg(__VA_ARGS__)
struct _mem_entry { u64 gpuaddr; };
struct kgsl_process_private { int *pid; char *comm; };
struct kgsl_context { int tid; };
struct adreno_gpudev { const char *(*iommu_fault_block)(struct kgsl_device *,u32); };
static struct adreno_gpudev test_gpudev;
static int proc_pid = 77;
static struct kgsl_process_private proc = {&proc_pid,"process"};
static int pid_nr(int *p){return *p;}
static struct kgsl_process_private *kgsl_iommu_get_process(u64 root){assert(root==0x12345678000);return &proc;}
static struct kgsl_iommu_context *iommu_context_by_name(struct kgsl_iommu *i,u32 name){return &i->user_context;}
static bool mutex_trylock(int *p){return !mutex_busy;}
static void mutex_unlock(int *p){}
static bool gmu_core_isenabled(struct kgsl_device *d){return true;}
static void kgsl_pwrctrl_irq(struct kgsl_device *d,int value){}
static int kgsl_pwrctrl_change_state(struct kgsl_device *d,int state){return 0;}
static void trace_kgsl_mmu_pagefault(struct kgsl_device *d,unsigned long addr,int pid,const char *comm,const char *rw){assert(pid==77);}
static int kgsl_mmu_log_fault_addr(struct kgsl_mmu *m,u64 root,u64 addr){return 0;}
static struct kgsl_context *kgsl_context_get(struct kgsl_device *d,u32 id){assert(id==99);return NULL;}
static void kgsl_context_put(struct kgsl_context *c){}
static void _check_if_freed(struct kgsl_iommu_context *c,u64 addr,int pid){freed_checks++;}
static bool kgsl_iommu_addr_is_global(struct kgsl_mmu *m,u64 addr){return false;}
static void _find_mem_entries(struct kgsl_mmu *m,u64 addr,struct _mem_entry *prev,struct _mem_entry *next,struct kgsl_process_private *p){prev->gpuaddr=0;next->gpuaddr=~0ULL;}
static void _print_entry(struct kgsl_device *d,struct _mem_entry *e){}
static void adreno_set_gpu_fault(struct adreno_device *a,int fault){client_fault|=fault;}
static void adreno_dispatcher_schedule(struct kgsl_device *d){scheduled++;if(nested_recovery){assert(!kgsl_iommu_pagefault_resume(&d->mmu));assert(!kgsl_iommu_clear_fsr(&d->mmu));}}
static void kgsl_process_private_put(struct kgsl_process_private *p){assert(p==&proc);private_puts++;}
static void get_fault_info(const void *cookie,struct adreno_smmu_fault_info *info){assert(cookie==&fixture&&refs&&!locked);*info=(struct adreno_smmu_fault_info){.fsr=fsr,.ttbr0=0x12345678000,.contextidr=99};}
#include "fault-handler.h"

static int report_iommu_fault(struct iommu_domain *d,void *dev,unsigned long addr,int flags) {
 assert(d==&fixture.domain&&!locked&&refs&&addr==0x12345000&&flags==IOMMU_FAULT_WRITE);reports++;
 if(qs.fault_hold_enabled&& (fsr&ARM_SMMU_CB_FSR_SS))assert(qs.fault_masked&&qs.fault_pending&&!(sctlr&ARM_SMMU_SCTLR_CFIE)&&refs==2);
 if(use_kgsl_callback)return kgsl_iommu_fault_handler(d,NULL,addr,flags,gpu.dev.mmu.defaultpagetable);
 if(nested_recovery){assert(!qcom_adreno_smmu_terminate_fault(&fixture));assert(!qcom_adreno_smmu_rearm_fault(&fixture));}
 return report_ret;
}
static struct kgsl_iommu_pt ipt;
static struct kgsl_pagetable pt;
static void reset(void) {
 assert(!locked);memset(&qs,0,sizeof(qs));memset(&fixture,0,sizeof(fixture));memset(&gpu,0,sizeof(gpu));
 qs.smmu.dev=&qs;fixture.smmu=&qs.smmu;fixture.cfg.cbndx=3;
 refs=pm_error=bank_error=writes=clears=resumes=reports=nested_recovery=stuck=0;report_ret=-EBUSY;fsr=0;last_resume=99;sctlr=ARM_SMMU_SCTLR_CFIE|ARM_SMMU_SCTLR_M|ARM_SMMU_SCTLR_HUPCF;
 use_kgsl_callback=mutex_busy=false;scheduled=client_fault=private_puts=freed_checks=0;
 ipt.attached=true;pt.priv=&ipt;pt.mmu=&gpu.dev.mmu;gpu.dev.mmu.device=&gpu.dev;gpu.dev.mmu.defaultpagetable=&pt;gpu.ft_pf_policy=BIT(KGSL_FT_PAGEFAULT_GPUHALT_ENABLE);
 gpu.dev.smmu=(struct adreno_smmu_priv){&fixture,qcom_adreno_smmu_set_fault_policy,qcom_adreno_smmu_terminate_fault,qcom_adreno_smmu_rearm_fault,get_fault_info};
}
static void held(void) {
 assert(!_setup_user_context(&gpu.dev.mmu));assert(!refs&&qs.fault_hold_enabled==BIT(3)&&(sctlr&ARM_SMMU_SCTLR_CFCFG)&&!(sctlr&ARM_SMMU_SCTLR_HUPCF));
 fsr=ARM_SMMU_CB_FSR_TF|ARM_SMMU_CB_FSR_SS;assert(qcom_adreno_smmu_context_fault(0,&fixture)==IRQ_HANDLED);
 gpu.dev.mmu.iommu.user_context.stalled_on_fault=true;
 assert(refs==1&&qs.fault_pending==BIT(3)&&qs.fault_masked==BIT(3)&&!clears&&!resumes&&reports==1&&!(sctlr&ARM_SMMU_SCTLR_CFIE));
}
int main(void) {
 reset();assert(qcom_adreno_smmu_context_fault(0,&fixture)==IRQ_NONE&&!refs&&!writes&&!reports);cases++;
 reset();pm_error=-EIO;assert(qcom_adreno_smmu_context_fault(0,&fixture)==IRQ_NONE&&!refs&&!writes);assert(_setup_user_context(&gpu.dev.mmu)==-EIO&&!qs.stall_enabled&&!writes);cases++;
 reset();bank_error=-ENODEV;assert(_setup_user_context(&gpu.dev.mmu)==-ENODEV&&!writes&&!refs);cases++;
 reset();fixture.cfg.cbndx=32;assert(_setup_user_context(&gpu.dev.mmu)==-EOPNOTSUPP&&!writes&&!refs);cases++;
 reset();held();int w=writes;assert(!_setup_user_context(&gpu.dev.mmu)&&writes==w&&refs==1);assert(kgsl_iommu_set_pf_policy(&gpu.dev.mmu,0)==-EBUSY&&refs==1);cases++;
 assert(qcom_adreno_smmu_context_fault(0,&fixture)==IRQ_HANDLED&&reports==1&&!resumes&&refs==1);cases++;
 assert(kgsl_iommu_clear_fsr(&gpu.dev.mmu)==-EBUSY&&gpu.dev.pwrctrl.transition_error==-EBUSY&&refs==1&&gpu.dev.mmu.iommu.user_context.stalled_on_fault);cases++;
 assert(!kgsl_iommu_pagefault_resume(&gpu.dev.mmu)&&!qs.fault_pending&&qs.fault_masked&&refs==1&&last_resume==ARM_SMMU_RESUME_TERMINATE);assert(!(sctlr&(ARM_SMMU_SCTLR_CFCFG|ARM_SMMU_SCTLR_HUPCF|ARM_SMMU_SCTLR_CFIE)));cases++;
 assert(!kgsl_iommu_clear_fsr(&gpu.dev.mmu)&&!refs&&!qs.fault_masked&&!gpu.dev.mmu.iommu.user_context.stalled_on_fault);assert((sctlr&ARM_SMMU_SCTLR_CFCFG)&&(sctlr&ARM_SMMU_SCTLR_CFIE)&&!(sctlr&ARM_SMMU_SCTLR_HUPCF));assert(gpu.dev.pwrctrl.transition_error==-EBUSY);cases++;
 reset();held();stuck=1;assert(kgsl_iommu_pagefault_resume(&gpu.dev.mmu)==-ETIMEDOUT&&qs.fault_pending&&qs.fault_masked&&refs==1&&gpu.dev.pwrctrl.transition_error==-ETIMEDOUT);assert(kgsl_iommu_clear_fsr(&gpu.dev.mmu)==-EBUSY&&gpu.dev.pwrctrl.transition_error==-ETIMEDOUT);cases++;
 reset();held();w=writes;pm_error=-EIO;assert(kgsl_iommu_pagefault_resume(&gpu.dev.mmu)==-EIO&&writes==w&&refs==1&&qs.fault_pending);assert(kgsl_iommu_clear_fsr(&gpu.dev.mmu)==-EIO&&writes==w&&refs==1);cases++;
 reset();held();assert(!kgsl_iommu_pagefault_resume(&gpu.dev.mmu));w=writes;pm_error=-EIO;assert(kgsl_iommu_clear_fsr(&gpu.dev.mmu)==-EIO&&writes==w&&refs==1&&qs.fault_masked);cases++;
 for(int opt=0;opt<2;opt++)for(int r=0;r<3;r++){
  reset();if(opt)assert(!_setup_user_context(&gpu.dev.mmu));report_ret=(int[]){0,-EAGAIN,-ENOSYS}[r];fsr=ARM_SMMU_CB_FSR_TF|ARM_SMMU_CB_FSR_SS;
  assert(qcom_adreno_smmu_context_fault(0,&fixture)==IRQ_HANDLED&&!refs&&!qs.fault_masked&&!qs.fault_pending&&resumes==1&&clears==1);assert(last_resume==(report_ret==-EAGAIN?0:ARM_SMMU_RESUME_TERMINATE));cases++;
 }
 reset();fsr=ARM_SMMU_CB_FSR_TF|ARM_SMMU_CB_FSR_SS;assert(qcom_adreno_smmu_context_fault(0,&fixture)==IRQ_HANDLED&&!refs&&resumes==1&&last_resume==ARM_SMMU_RESUME_TERMINATE);cases++;
 reset();assert(!_setup_user_context(&gpu.dev.mmu));fsr=ARM_SMMU_CB_FSR_PF;assert(qcom_adreno_smmu_context_fault(0,&fixture)==IRQ_HANDLED&&!refs&&!resumes&&clears==1);cases++;
 reset();assert(!_setup_user_context(&gpu.dev.mmu));nested_recovery=1;fsr=ARM_SMMU_CB_FSR_TF|ARM_SMMU_CB_FSR_SS;assert(qcom_adreno_smmu_context_fault(0,&fixture)==IRQ_HANDLED&&!refs&&!qs.fault_pending&&!qs.fault_masked&&resumes==1);cases++;
 reset();assert(!_setup_user_context(&gpu.dev.mmu));assert(!kgsl_iommu_set_pf_policy(&gpu.dev.mmu,0)&&!refs&&!qs.fault_hold_enabled&&!qs.stall_enabled&&!(sctlr&ARM_SMMU_SCTLR_CFCFG)&&(sctlr&ARM_SMMU_SCTLR_HUPCF));cases++;
 reset();gpu.dev.smmu.terminate_fault=NULL;assert(_setup_user_context(&gpu.dev.mmu)==-ENODEV);assert(kgsl_iommu_pagefault_resume(&gpu.dev.mmu)==-ENODEV);cases++;
 reset();ipt.attached=false;assert(_setup_user_context(&gpu.dev.mmu)==-ENODEV&&!writes);cases++;
 reset();assert(kgsl_iommu_clear_fsr(&gpu.dev.mmu)==-ENODEV&&!writes);cases++;
 reset();assert(!_setup_user_context(&gpu.dev.mmu));use_kgsl_callback=true;fsr=ARM_SMMU_CB_FSR_TF|ARM_SMMU_CB_FSR_SS;
 assert(qcom_adreno_smmu_context_fault(0,&fixture)==IRQ_HANDLED&&refs==1&&scheduled==1&&client_fault==ADRENO_IOMMU_PAGE_FAULT&&private_puts==1&&freed_checks==1&&gpu.dev.mmu.iommu.user_context.stalled_on_fault);assert(!resumes&&!clears);assert(!kgsl_iommu_pagefault_resume(&gpu.dev.mmu));assert(!kgsl_iommu_clear_fsr(&gpu.dev.mmu)&&!refs);cases++;
 reset();assert(!_setup_user_context(&gpu.dev.mmu));use_kgsl_callback=true;mutex_busy=true;fsr=ARM_SMMU_CB_FSR_PF|ARM_SMMU_CB_FSR_SS;
 assert(qcom_adreno_smmu_context_fault(0,&fixture)==IRQ_HANDLED&&!refs&&!scheduled&&!client_fault&&resumes==1&&private_puts==1&&!freed_checks);cases++;
 reset();assert(!_setup_user_context(&gpu.dev.mmu));use_kgsl_callback=true;nested_recovery=1;fsr=ARM_SMMU_CB_FSR_EF|ARM_SMMU_CB_FSR_SS;
 assert(qcom_adreno_smmu_context_fault(0,&fixture)==IRQ_HANDLED&&!refs&&scheduled==1&&resumes==1&&!qs.fault_masked&&!gpu.dev.mmu.iommu.user_context.stalled_on_fault);cases++;
 printf("%d actual provider/consumer fault scenarios passed; MMIO/PM/IRQ callback mocked\n",cases);
 return 0;
}
