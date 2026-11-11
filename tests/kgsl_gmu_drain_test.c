/* Real core entry and Qualcomm poll; MMIO, locks and runtime PM are models. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef uint32_t u32;
typedef uint64_t phys_addr_t;
typedef unsigned gfp_t;
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define ARM_SMMU_DOMAIN_S1 1
#define ARM_SMMU_CTX_FMT_AARCH64 3
#define ARM_SMMU_CB_S1_TLBIASID 0x610
#define ARM_SMMU_CB_TLBSYNC 0x7f0
#define ARM_SMMU_CB_TLBSTATUS 0x7f4
#define ARM_SMMU_sTLBGSTATUS_GSACTIVE 1
#define QCOM_DUMMY_VAL 0xffffffffU
#define TLB_LOOP_TIMEOUT 8
#define TLB_SPIN_COUNT 3
#define ARM_SMMU_CB(s,b) (0x1000 + (b))
struct iommu_ops { int unused; };
static const struct iommu_ops arm_smmu_ops, other_ops;
struct iommu_domain { const struct iommu_ops *owner; };
struct arm_smmu_cfg { unsigned fmt,cbndx,asid; };
struct arm_smmu_cb { struct arm_smmu_cfg *cfg; };
struct arm_smmu_device;
struct arm_smmu_impl { int (*tlb_sync_checked)(struct arm_smmu_device *,int,int,int); };
struct arm_smmu_device { unsigned num_context_banks; struct arm_smmu_cb cbs[8]; struct arm_smmu_impl *impl; };
struct iommu_iotlb_gather { int unused; };
struct iommu_flush_ops {
 void (*tlb_flush_all)(void *);
 void (*tlb_flush_walk)(unsigned long,size_t,size_t,void *);
 void (*tlb_add_page)(struct iommu_iotlb_gather *,unsigned long,size_t,void *);
 int (*tlb_flush_walk_checked)(unsigned long,size_t,size_t,void *);
};
static const struct iommu_flush_ops arm_smmu_s1_tlb_ops;
struct io_pgtable_ops {
 int (*map_pages)(struct io_pgtable_ops *, unsigned long, phys_addr_t, size_t, size_t, int, gfp_t, size_t *);
 size_t (*unmap_pages)(struct io_pgtable_ops *, unsigned long, size_t, size_t, struct iommu_iotlb_gather *);
};
struct arm_smmu_domain {
 struct iommu_domain domain; struct arm_smmu_device *smmu; struct io_pgtable_ops *pgtbl_ops; bool checked_unmap;
 struct arm_smmu_cfg cfg; unsigned stage; int init_mutex,cb_lock;
};
static struct arm_smmu_domain sd;
static struct arm_smmu_device smmu;
static struct arm_smmu_impl impl;
static int pm_error, pm_gets,pm_puts,users,barriers,invalidates,syncs,reads,debugs,clear_after;
static bool init_locked,cb_locked,expect_init_lock;
static unsigned legacy_walks,map_calls,unmap_calls;
static int map_error;
static bool trigger_walk;
static struct arm_smmu_domain *to_smmu_domain(struct iommu_domain *d) { assert(d==&sd.domain);return &sd; }
static void mutex_lock(int *p) {assert(p==&sd.init_mutex && !init_locked);init_locked=true;}
static void mutex_unlock(int *p) {assert(p==&sd.init_mutex && init_locked && !cb_locked);init_locked=false;}
#define spin_lock_irqsave(p,f) do {assert((p)==&sd.cb_lock && init_locked==expect_init_lock && !cb_locked && users==1);(f)=0;cb_locked=true;} while(0)
#define spin_unlock_irqrestore(p,f) do {assert((p)==&sd.cb_lock && cb_locked && !(f));cb_locked=false;} while(0)
static int arm_smmu_rpm_get(struct arm_smmu_device *s) {assert(s==&smmu && init_locked==expect_init_lock);pm_gets++;if(pm_error)return pm_error;users++;return 0;}
static void arm_smmu_rpm_put(struct arm_smmu_device *s) {assert(s==&smmu && users==1 && !cb_locked);users--;pm_puts++;}
static void wmb(void) {assert(cb_locked && users==1);barriers++;}
static void arm_smmu_cb_write(struct arm_smmu_device *s,unsigned bank,unsigned reg,u32 value) {
 assert(s==&smmu && cb_locked && users==1 && barriers==1);
 assert(bank==sd.cfg.cbndx && reg==ARM_SMMU_CB_S1_TLBIASID && value==sd.cfg.asid);invalidates++;
}
static void arm_smmu_writel(struct arm_smmu_device *s,int page,int reg,u32 value) {
 assert(s==&smmu && cb_locked && users==1 && invalidates==1);
 assert(page==(int)ARM_SMMU_CB(s,sd.cfg.cbndx) && reg==ARM_SMMU_CB_TLBSYNC && value==QCOM_DUMMY_VAL);syncs++;
}
static u32 arm_smmu_readl(struct arm_smmu_device *s,int page,int reg) {
 assert(s==&smmu && cb_locked && users==1 && syncs==1);
 assert(page==(int)ARM_SMMU_CB(s,sd.cfg.cbndx) && reg==ARM_SMMU_CB_TLBSTATUS);
 reads++;return clear_after>=0 && reads>clear_after?0:ARM_SMMU_sTLBGSTATUS_GSACTIVE;
}
static void cpu_relax(void) {}
static void udelay(unsigned d) {assert(d>0 && d<TLB_LOOP_TIMEOUT);}
static void qcom_smmu_tlb_sync_debug(struct arm_smmu_device *s) {assert(s==&smmu && cb_locked);debugs++;}
static void arm_smmu_tlb_inv_walk_s1(unsigned long iova,size_t size,size_t granule,void *cookie) {
 assert(cookie==&sd && users==1 && size==4096 && granule==4096 && iova==0x60000000);legacy_walks++;
}
static int arm_smmu_tlb_inv_walk_s1_checked(unsigned long,size_t,size_t,void *);
static void arm_smmu_tlb_inv_context_s1(void *cookie) { assert(0); }
static void arm_smmu_tlb_add_page_s1(struct iommu_iotlb_gather *g,unsigned long i,size_t n,void *cookie) { assert(0); }
static int map_pages(struct io_pgtable_ops *ops,unsigned long iova,phys_addr_t pa,size_t size,size_t count,int prot,gfp_t gfp,size_t *mapped) {
 assert(users==1 && iova==0x60000000 && pa==0x123000 && size==4096 && count==1);map_calls++;
 if(!map_error) *mapped=size*count;
 return map_error;
}
static size_t unmap_pages(struct io_pgtable_ops *ops,unsigned long iova,size_t size,size_t count,struct iommu_iotlb_gather *gather) {
 assert(users==1 && iova==0x60000000 && size==4096 && count==1);unmap_calls++;
 if(trigger_walk) {
  assert(arm_smmu_s1_tlb_ops.tlb_flush_walk_checked);
  if(arm_smmu_s1_tlb_ops.tlb_flush_walk_checked(iova,size,size,&sd))return 0;
 }
 return size*count;
}
static struct io_pgtable_ops pgops={map_pages,unmap_pages};
#include "gmu-drain-functions.h"
static void reset(void) {
 sd=(struct arm_smmu_domain){.domain={&arm_smmu_ops},.smmu=&smmu,.pgtbl_ops=&pgops,.cfg={3,5,0x135},.stage=1};
 impl=(struct arm_smmu_impl){qcom_smmu_tlb_sync_checked};
 smmu=(struct arm_smmu_device){.num_context_banks=8,.impl=&impl};smmu.cbs[5].cfg=&sd.cfg;
 pm_error=pm_gets=pm_puts=users=barriers=invalidates=syncs=reads=debugs=clear_after=0;
 init_locked=cb_locked=false;expect_init_lock=true;legacy_walks=map_calls=unmap_calls=0;map_error=0;trigger_walk=false;
}
int main(void) {
 unsigned cases=0;
 for(unsigned n=0;n<14;n++) {
  reset();struct iommu_domain *d=&sd.domain;int expected=0;
  if(n==1){d=NULL;expected=-EOPNOTSUPP;}
  if(n==2){sd.domain.owner=&other_ops;expected=-EOPNOTSUPP;}
  if(n==3){sd.smmu=NULL;expected=-ENODEV;}
  if(n==4){sd.pgtbl_ops=NULL;expected=-ENODEV;}
  if(n==5){sd.stage=2;expected=-EOPNOTSUPP;}
  if(n==6){sd.cfg.fmt=1;expected=-EOPNOTSUPP;}
  if(n==7){sd.cfg.cbndx=8;expected=-EOPNOTSUPP;}
  if(n==8){smmu.cbs[5].cfg=NULL;expected=-EOPNOTSUPP;}
  if(n==9){smmu.impl=NULL;expected=-EOPNOTSUPP;}
  if(n==10){impl.tlb_sync_checked=NULL;expected=-EOPNOTSUPP;}
  if(n==11){pm_error=-EACCES;expected=-EACCES;}
  if(n==12){clear_after=4;}
  if(n==13){clear_after=-1;expected=-ETIMEDOUT;}
  assert(arm_smmu_flush_tlb_checked(d)==expected);
  assert(!init_locked && !cb_locked && !users);
  if(n==0 || n>=12)assert(pm_gets==1 && pm_puts==1 && invalidates==1 && syncs==1 && barriers==1);
  else assert(!invalidates && !syncs && !pm_puts && pm_gets==(n==11));
  assert(debugs==(n==13));cases++;
 }
 for(unsigned bank=0;bank<8;bank++) {
  reset();smmu.cbs[5].cfg=NULL;sd.cfg.cbndx=bank;sd.cfg.asid=0xab00+bank;smmu.cbs[bank].cfg=&sd.cfg;
  assert(!arm_smmu_flush_tlb_checked(&sd.domain) && invalidates==1 && syncs==1);cases++;
 }
 for(unsigned n=0;n<3;n++) {
  reset();int expected=0;
  if(n==1){pm_error=-EACCES;expected=-EACCES;}
  if(n==2){clear_after=-1;expected=-ETIMEDOUT;}
  assert(arm_smmu_enable_checked_unmap(&sd.domain)==expected);
  assert(sd.checked_unmap==!expected && !users && !init_locked && !cb_locked);cases++;
 }
 for(unsigned n=0;n<7;n++) {
  reset();expect_init_lock=false;size_t mapped=0;
  if(n==1)pm_error=-EACCES;
  if(n==2)sd.pgtbl_ops=NULL;
  if(n==3)map_error=-ENOMEM;
  if(n>=4){trigger_walk=true;sd.checked_unmap=n>=5;}
  if(n==6)clear_after=-1;
  int ret=arm_smmu_map_pages(&sd.domain,0x60000000,0x123000,4096,1,3,0,&mapped);
  assert(ret==(n==1?-EACCES:n==2?-ENODEV:n==3?-ENOMEM:0));
  assert(mapped==(!ret?4096:0));
  size_t unmapped=arm_smmu_unmap_pages(&sd.domain,0x60000000,4096,1,NULL);
  assert(unmapped==((n==1 || n==2 || n==6)?0:4096));
  assert(!users && !cb_locked && !init_locked);
  assert(map_calls==(n!=1 && n!=2) && unmap_calls==(n!=1 && n!=2));
  assert(legacy_walks==(n==4) && syncs==(n>=5));
  assert(pm_puts==(n==1 || n==2?0:2));cases++;
 }
 printf("PASS: %u checked SMMU drain scenarios; actual poll/core, modeled MMIO/PM/locks\n",cases);
 return 0;
}
