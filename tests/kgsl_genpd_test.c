#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define EPROBE_DEFER 517
typedef uint32_t u32;
typedef uint64_t u64;
#define PAGE_SIZE 4096U
#define U32_MAX UINT32_MAX
#define IS_ALIGNED(x,a) (!((x)&((a)-1)))
#define GMU_DISPATCH 1
#define test_bit(n,p) (!!(*(p)&(1UL<<(n))))
#define IS_ERR_OR_NULL(p) (!(p)||(uintptr_t)(p)>=(uintptr_t)-4095)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define dev_err(...) ((void)0)
struct device {int id,users,get_error,put_error,gets,puts;bool on,stuck,synced;void *of_node;};
struct platform_device {struct device dev;};
struct kgsl_device {void *dev; struct {unsigned long flags;} gmu_core;};
struct iommu_domain {int unused;};
struct gmu_memdesc {void *hostptr;uint32_t gmuaddr,size;uint64_t dma_handle;};
struct a6xx_gmu_device {struct iommu_domain *domain;unsigned global_entries;struct gmu_memdesc gmu_globals[3];struct platform_device *pdev;struct device *cxpd,*gxpd;bool native_domains,cx_held,gx_held,clks_on;int power_error,num_clks;void *clks,*cx_gdsc,*qmp,*fw_image,*itcm_shadow;};
struct adreno_device {struct kgsl_device dev;struct a6xx_gmu_device gmu;bool target,acd_enabled;};
#define KGSL_DEVICE(a) (&(a)->dev)
#define ADRENO_DEVICE(d) ((struct adreno_device *)(d))
static struct a6xx_gmu_device *to_a6xx_gmu(struct adreno_device *a) {return &a->gmu;}
static bool adreno_is_a680(struct adreno_device *a) {return a->target;}
static struct device cx,gx;static struct platform_device pdev;
static struct device *cx_result,*gx_result;
static bool compatible,hw_gx_on;
static unsigned attaches,detaches,reads,clocks_off,legacy_calls,frees;
static int legacy_error;
static struct iommu_domain domain;
static unsigned unmaps,dma_frees,domain_detaches,domain_frees,bad_unmap_call;
static size_t bad_unmap_size;
static unsigned drain_calls, bad_drain_call, imports_calls;
static int drain_error, imports_error;
static int arm_smmu_flush_tlb_checked(struct iommu_domain *d) {
 assert(d==&domain && unmaps);drain_calls++;
 return drain_calls==bad_drain_call?drain_error:0;
}
static int a6xx_hwsched_free_gmu_imports(struct adreno_device *a) {
 imports_calls++; assert(!unmaps && !domain_detaches);return imports_error;
}
static unsigned char backing[3][4096];
static bool mapped[3],live[3];
static size_t iommu_unmap(struct iommu_domain *d,uint32_t addr,uint32_t size)
{
 assert(d==&domain && !domain_detaches && size==4096);
 unsigned i=(addr-0x60000000)/4096;
 assert(i<3 && live[i] && mapped[i]);
 unmaps++;
 if(unmaps==bad_unmap_call)return bad_unmap_size;
 mapped[i]=false;
 return size;
}
static void dma_free_attrs(struct device *d,size_t size,void *ptr,uint64_t handle,unsigned long attrs)
{
 unsigned i=((uintptr_t)ptr-(uintptr_t)backing)/4096;
 assert(d==&pdev.dev && size==4096 && !attrs && i<3 && ptr==backing[i]);
 assert(handle==0xfedc0000ULL+i*4096 && live[i] && !mapped[i]);
 assert(drain_calls && drain_calls!=bad_drain_call);
 live[i]=false;dma_frees++;
}
static void iommu_detach_device(struct iommu_domain *d,struct device *dev)
{
 assert(d==&domain && dev==&pdev.dev && !domain_detaches);
 for(unsigned i=0;i<3;i++)assert(!live[i]);
 domain_detaches++;
}
static void iommu_domain_free(struct iommu_domain *d)
{
 assert(d==&domain && domain_detaches==1 && !domain_frees);domain_frees++;
}
static int pm_runtime_resume_and_get(struct device *d) {++d->gets;
  if(d->get_error){return d->get_error;}
  ++d->users;d->on=true;return 0;}
static void dev_pm_genpd_synced_poweroff(struct device *d) {d->synced=true;}
static int pm_runtime_put_sync_suspend(struct device *d) {assert(d->users>0 && d->synced);d->synced=false;--d->users;++d->puts;
  if(d->put_error)return d->put_error;
  if(!d->stuck){d->on=false;}
  return 0;}
static bool dev_pm_genpd_is_on(struct device *d) {++reads;return d->on;}
#define read_poll_timeout(fn,v,cond,sleep,timeout,before,pd) ({int rc=-ETIMEDOUT;assert(sleep==100 && timeout==5000000 && !before);for(int n=0;n<3;++n){v=fn(pd);if(cond){rc=0;break;}}rc;})
static struct device *dev_pm_domain_attach_by_name(struct device *d,const char *name) {++attaches;
  if(!strcmp(name,"cx"))return cx_result;
  assert(!strcmp(name,"gx"));return gx_result;}
static void dev_pm_domain_detach(struct device *d,bool off) {assert(d && !d->users && !off);++detaches;}
static bool of_device_is_compatible(void *node,const char *name) {assert(!strcmp(name,"qcom,adreno-gmu"));return compatible;}
static int a6xx_gmu_regulators_probe(struct a6xx_gmu_device *g,struct platform_device *p) {++legacy_calls;return legacy_error;}
static int regulator_enable(void *r) {++legacy_calls;return legacy_error;}
static int a6xx_cx_regulator_disable_wait(void *r,struct kgsl_device *d,int t) {++legacy_calls;return legacy_error;}
static bool a6xx_gmu_gx_is_on(struct kgsl_device *d) {return hw_gx_on;}
static void clk_bulk_disable_unprepare(int n,void *c) {assert(!gx.on || legacy_calls);++clocks_off;}
static void qmp_put(void *p) {if(p)++frees;}
static void release_firmware(void *p) {assert(p);++frees;}
static void vfree(void *p) {if(p)++frees;}
#include "kgsl-genpd-functions.h"
static struct adreno_device reset(void) {
 cx=(struct device){.id=1};gx=(struct device){.id=2};cx_result=&cx;gx_result=&gx;
 compatible=true;hw_gx_on=false;attaches=detaches=reads=clocks_off=legacy_calls=frees=0;legacy_error=0;
 unmaps=dma_frees=domain_detaches=domain_frees=bad_unmap_call=0;bad_unmap_size=0;
 drain_calls=bad_drain_call=imports_calls=0;drain_error=imports_error=0;
 memset(mapped,0,sizeof(mapped));memset(live,0,sizeof(live));
 return (struct adreno_device){.target=true,.gmu={.pdev=&pdev,.fw_image=(void *)1,.qmp=(void *)2,.itcm_shadow=(void *)3}};
}
static struct adreno_device running(void) {
 struct adreno_device a=reset();
  assert(!a6xx_gmu_power_probe(&a));
  assert(!a6xx_gmu_enable_gdsc(&a));a.gmu.clks_on=true;return a;
}
int main(void) {
 unsigned cases=0;
 for(int n=0;n<7;++n) {
  struct adreno_device a=reset();int want=0;
  if(n==1){cx_result=NULL;want=-ENODEV;}if(n==2){cx_result=ERR_PTR(-EPROBE_DEFER);want=-EPROBE_DEFER;}
  if(n==3){gx_result=NULL;want=-ENODEV;}if(n==4){gx_result=ERR_PTR(-EPROBE_DEFER);want=-EPROBE_DEFER;}
  if(n==5){compatible=false;want=-EOPNOTSUPP;}if(n==6){a.target=false;legacy_error=-EIO;want=-EIO;}
  assert(a6xx_gmu_power_probe(&a)==want);
  assert(a.gmu.native_domains==!want && !cx.users && !gx.users);
  assert(detaches==(n==3 || n==4));
  if(n==6)assert(legacy_calls==1 && !attaches);
  ++cases;
 }
 for(int n=0;n<8;++n) {
  struct adreno_device a=reset();
  assert(!a6xx_gmu_power_probe(&a));int want=0;
  if(n==1){cx.get_error=-EIO;want=-EIO;}if(n>=2 && n<=4){gx.get_error=-EAGAIN;want=-EAGAIN;}
  if(n==3)cx.put_error=-EACCES;
  if(n==4)cx.stuck=true;
  if(n==5){a.gmu.cx_held=true;want=-EBUSY;}if(n==6){a.gmu.power_error=-EIO;want=-EIO;}if(n==7){a.gmu.native_domains=false;want=-ENODEV;}
  assert(a6xx_gmu_enable_gdsc(&a)==want);
  if(n==0)assert(cx.users==1 && gx.users==1 && a.gmu.cx_held && a.gmu.gx_held && !hw_gx_on);
  else assert(!gx.users && !cx.users);
  if(n==3)assert(a.gmu.power_error==-EACCES);
  if(n==4)assert(a.gmu.power_error==-ETIMEDOUT);
  if(n>=5)assert(!cx.gets && !gx.gets);
  ++cases;
 }
 for(int n=0;n<6;++n) {
  struct adreno_device a=running();int want=0;
  if(n==1){gx.put_error=-EACCES;want=-EACCES;}if(n==2){gx.stuck=true;want=-ETIMEDOUT;}if(n==3){hw_gx_on=true;want=-ETIMEDOUT;}
  if(n==4)a.gmu.clks_on=false;
  if(n==5)assert(!a6xx_gmu_domain_put(&gx,&a.gmu.gx_held));
  assert(a6xx_gmu_disable_clks(&a)==want);
  assert(!gx.users && !a.gmu.gx_held && gx.puts==1 && cx.users==1);
  assert(clocks_off==(!want && n!=4) && a.gmu.power_error==want);
  if(want)assert(a.gmu.clks_on);
  ++cases;
 }
 for(int n=0;n<7;++n) {
  struct adreno_device a=running();int want=0;
  if(n==1)want=-EBUSY;else a.gmu.clks_on=false;
  if(n==2){gx.put_error=-EIO;want=-EIO;}if(n==3){cx.put_error=-EACCES;want=-EACCES;}if(n==4){cx.stuck=true;want=-ETIMEDOUT;}
  if(n==5){assert(!a6xx_gmu_domain_put(&gx,&a.gmu.gx_held));
  assert(!a6xx_gmu_domain_put(&cx,&a.gmu.cx_held));}
  if(n==6){a=reset();legacy_error=-EOPNOTSUPP;want=-EOPNOTSUPP;}
  assert(a6xx_gmu_disable_cx(&a)==want && a.gmu.power_error==want);
  if(n==1)assert(!gx.puts && !cx.puts);
  if(n==2)assert(!cx.puts && a.gmu.cx_held);
  if(!want)assert(!gx.users && !cx.users && !a.gmu.cx_held && !a.gmu.gx_held);
  if(n==6)assert(legacy_calls==1);
  ++cases;
 }
 for(int n=0;n<4;++n) {
  struct adreno_device a=reset();
  assert(!a6xx_gmu_power_probe(&a));
  if(n==1)a.gmu.cx_held=true;
  if(n==2)a.gmu.power_error=-EIO;
  if(n==3)a.gmu.clks_on=true;
  a6xx_gmu_remove(&a.dev);
  assert(n?(!detaches && !frees):(detaches==2 && frees==3 && !a.gmu.native_domains));
  ++cases;
 }
 {bool held=true;
  assert(a6xx_gmu_domain_put(NULL,&held)==-ENODEV && held);
  assert(a6xx_gmu_domain_put(ERR_PTR(-EIO),&held)==-EIO && held);
  ++cases;}
 {struct adreno_device a=reset();
  assert(!a6xx_gmu_power_probe(&a));for(int i=0;i<2;++i){assert(!a6xx_gmu_enable_gdsc(&a));a.gmu.clks_on=true;
  assert(!a6xx_gmu_disable_clks(&a));
  assert(!a6xx_gmu_disable_cx(&a));
  assert(!cx.users && !gx.users);}a6xx_gmu_remove(&a.dev);
  assert(cx.gets==cx.puts && gx.gets==gx.puts && detaches==2);
  ++cases;}
 /* Remove must stop before firmware/QMP/genpd release if any backing is uncertain. */
 for(unsigned scenario=0;scenario<11;scenario++) {
  struct adreno_device a=reset();
  assert(!a6xx_gmu_power_probe(&a));
  a.gmu.domain=&domain;a.gmu.global_entries=3;
  for(unsigned i=0;i<3;i++) {
   a.gmu.gmu_globals[i]=(struct gmu_memdesc){backing[i],0x60000000+i*4096,4096,0xfedc0000ULL+i*4096};
   live[i]=mapped[i]=true;
  }
  struct gmu_memdesc saved[3];memcpy(saved,a.gmu.gmu_globals,sizeof(saved));
  if(scenario>=1 && scenario<=5) {
   bad_unmap_call=scenario<=3?1:2;
   bad_unmap_size=(scenario==1 || scenario==5)?0:scenario==3?8192:2048;
  }
  if(scenario==6)a.gmu.domain=NULL;
  if(scenario==7)a.gmu.power_error=-ETIMEDOUT;
  if(scenario>=8) {bad_drain_call=scenario==10?2:1;drain_error=scenario==9?-EACCES:-ETIMEDOUT;}
  a6xx_gmu_remove(&a.dev);
  if(!scenario) {
   assert(unmaps==3 && dma_frees==3 && domain_detaches==1 && domain_frees==1);
   assert(frees==3 && detaches==2 && !a.gmu.domain && !a.gmu.global_entries);
   assert(!a.gmu.fw_image && !a.gmu.itcm_shadow && !a.gmu.qmp);
   a6xx_gmu_remove(&a.dev);
   assert(unmaps==3 && dma_frees==3 && frees==3 && detaches==2 && domain_frees==1);
  } else {
   unsigned prefix=(scenario==4 || scenario==5 || scenario==10)?1:0;
   int expected=scenario>=8?drain_error:scenario==6?-ENODEV:scenario==7?-ETIMEDOUT:-EUCLEAN;
   assert(a.gmu.power_error==expected && a.gmu.global_entries==3);
   assert(a.gmu.domain==(scenario==6?NULL:&domain));
   assert(dma_frees==prefix && !domain_frees && !domain_detaches && !frees && !detaches);
   assert(a.gmu.fw_image==(void *)1 && a.gmu.qmp==(void *)2 && a.gmu.itcm_shadow==(void *)3 && a.gmu.native_domains);
   for(unsigned i=0;i<3;i++) {
    if(i<prefix)assert(!a.gmu.gmu_globals[i].hostptr && !live[i]);
    else assert(!memcmp(&a.gmu.gmu_globals[i],&saved[i],sizeof(saved[i])) && live[i]);
   }
   unsigned old_unmaps=unmaps;
   assert(a6xx_free_gmu_globals(&a.gmu)==expected);
   a6xx_gmu_remove(&a.dev);
   assert(unmaps==old_unmaps && dma_frees==prefix && !domain_frees && !frees && !detaches);
  }
  ++cases;
 }
 for(unsigned n=0;n<2;n++) {
  struct adreno_device a=reset();a.dev.gmu_core.flags=1UL<<GMU_DISPATCH;
  imports_error=n?-EUCLEAN:0;
  assert(a6xx_gmu_remove(&a.dev)==imports_error && imports_calls==1);
  assert(frees==(n?0:3));cases++;
 }
 printf("%u native GMU genpd ownership/failure scenarios passed; PM/provider/hardware mocked\n",cases);return 0;
}
