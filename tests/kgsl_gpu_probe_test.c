/* Real source functions, with kernel registration leaves and hardware mocked. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define __init
#define __exit
#define dev_crit(...) ((void)0)
#define dev_err(...) ((void)0)
#define pr_debug(...) ((void)0)
#define pr_err(...) ((void)0)
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define EPROBE_DEFER 517
#define ADRENO_DEPRECATED 1
#define IRQF_TRIGGER_HIGH 4
#define IRQF_NO_AUTOEN 0x80000
#define ERR_PTR(n) ((void *)(intptr_t)(n))
#define PTR_ERR(p) ((intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
typedef uint32_t u32;
typedef int (*irq_handler_t)(int, void *);
struct device_node { bool native, chip_present, gmu_ref; int refs; u32 chip; };
struct device { struct device_node *of_node; };
struct platform_device { struct device dev; };
struct kgsl_device { struct platform_device *pdev; struct { bool components_bound; } gmu_core; };
struct adreno_gpu_core { u32 features, core, major, minor, patchid; };
struct of_device_id { const char *compatible; void *data; };
struct platform_driver { int id; };
struct module { int unused; };
static struct module this_module;
#define THIS_MODULE (&this_module)
static struct platform_driver a6xx_gmu_driver={0}, a6xx_rgmu_driver={1}, adreno_platform_driver={2};
#define platform_register_drivers(d,n) __platform_register_drivers(d,n,THIS_MODULE)
#define platform_driver_register(d) __platform_driver_register(d,THIS_MODULE)
static int device_3d0;
static const int adreno_gmu_match[1];
static struct device_node gpu_node, gmu_node, revision_node;
static struct platform_device pdev;
static struct kgsl_device kgsl;
static struct adreno_gpu_core gpucore;
static int property_error, fuse_calls, revision_calls, find_calls, bind_calls, unbind_calls, bind_error;
static bool use_revision, have_gmu, no_core;
static int irq_value, irq_error, irq_lookup, irq_named, irq_requests;
static int core_error, driver_error[3], core_exits, trace[32], ntrace;
static bool driver_live[3], core_live;
static unsigned int cases;
static void event(int n) { assert(ntrace < (int)ARRAY_SIZE(trace)); trace[ntrace++]=n; }
static bool of_device_is_compatible(struct device_node *n, const char *s)
{ assert(!strcmp(s,"qcom,adreno-680.1")); return n->native; }
static bool of_property_present(struct device_node *n, const char *s)
{ if (!strcmp(s,"qcom,chipid")) return n->chip_present; assert(!strcmp(s,"qcom,gmu")); return n->gmu_ref; }
static int of_property_read_u32(struct device_node *n, const char *s, u32 *value)
{ assert(!strcmp(s,"qcom,chipid")); if(property_error) return property_error; if(!n->chip_present) return -EINVAL; *value=n->chip; return 0; }
static u32 adreno_efuse_read_soc_hw_rev(struct platform_device *p) { assert(p==&pdev); fuse_calls++; return 7; }
static struct device_node *of_node_get(struct device_node *n) { n->refs++; return n; }
static void of_node_put(struct device_node *n) { assert(n && n->refs>0); n->refs--; }
static struct device_node *adreno_get_soc_hw_revision_node(struct platform_device *p, u32 rev)
{ assert(p==&pdev && rev==7); revision_calls++; return use_revision ? of_node_get(&revision_node) : NULL; }
static const struct adreno_gpu_core *_get_gpu_core(struct platform_device *p, u32 chip)
{ assert(p==&pdev); if(no_core || chip != 0x06080001) return NULL; return &gpucore; }
static struct device_node *of_find_matching_node(struct device_node *from, const void *matches)
{ assert(!from && matches==adreno_gmu_match); find_calls++; return have_gmu ? of_node_get(&gmu_node) : NULL; }
static int component_bind_all(struct device *d, void *data)
{ assert(d==&pdev.dev && !data && !kgsl.gmu_core.components_bound); bind_calls++; return bind_error; }
static void component_unbind_all(struct device *d, void *data)
{ assert(d==&pdev.dev && !data && kgsl.gmu_core.components_bound); unbind_calls++; }
static int kgsl_irq_handler(int irq, void *data) { assert(!"GPU IRQ fired before startup"); return 0; }
static int platform_get_irq(struct platform_device *p, unsigned int index)
{ assert(p==&pdev && index==0); irq_lookup++; return irq_value; }
static int platform_get_irq_byname(struct platform_device *p, const char *name)
{ assert(p==&pdev && !strcmp(name,"kgsl_3d0_irq")); irq_named++; return irq_value; }
static int devm_request_irq(struct device *dev, int irq, irq_handler_t fn, unsigned long flags, const char *name, void *data)
{
 assert(dev==&pdev.dev && irq==irq_value && fn==kgsl_irq_handler && data==&kgsl);
 assert(!strcmp(name,"kgsl_3d0_irq")); irq_requests++;
 if(!(flags & IRQF_NO_AUTOEN)) fn(irq,data);
 assert(flags==(IRQF_TRIGGER_HIGH|IRQF_NO_AUTOEN)); return irq_error;
}
static int __platform_driver_register(struct platform_driver *driver, struct module *owner)
{
 int id=driver->id; assert(owner==THIS_MODULE && core_live && !driver_live[id]); event(10+id);
 if(driver_error[id]) return driver_error[id];
 driver_live[id]=true; return 0;
}
static void platform_driver_unregister(struct platform_driver *driver)
{ int id=driver->id; assert(driver_live[id]); event(20+id); driver_live[id]=false; }
static int kgsl_core_init(void) { event(1); if(core_error) return core_error; assert(!core_live); core_live=true; return 0; }
static void kgsl_core_exit(void)
{ assert(core_live && !driver_live[0] && !driver_live[1] && !driver_live[2]); core_live=false; core_exits++; event(2); }
#include "kgsl-gpu-probe-target.h"
#include "kgsl-gpu-probe-functions.h"
static void reset(void)
{
 assert(!kgsl.gmu_core.components_bound && !gmu_node.refs && !revision_node.refs);
 assert(!core_live && !driver_live[0] && !driver_live[1] && !driver_live[2]);
 if(cases) assert(gpu_node.refs==1);
 gpu_node=(struct device_node){.native=true,.gmu_ref=true,.refs=1,.chip=0x06080001};
 revision_node=(struct device_node){.chip_present=true,.chip=0x06080001};
 pdev.dev.of_node=&gpu_node; kgsl=(struct kgsl_device){.pdev=&pdev};
 gpucore=(struct adreno_gpu_core){.core=6,.major=8,.patchid=1};
 property_error=fuse_calls=revision_calls=find_calls=bind_calls=unbind_calls=bind_error=0;
 use_revision=no_core=false; have_gmu=true;
 irq_value=703; irq_error=irq_lookup=irq_named=irq_requests=0;
 core_error=core_exits=ntrace=0; memset(driver_error,0,sizeof(driver_error)); cases++;
}
#define EXPECT_TRACE(...) do { int expected[]={__VA_ARGS__}; assert(ntrace==(int)ARRAY_SIZE(expected)); assert(!memcmp(trace,expected,sizeof(expected))); } while(0)
int main(void)
{
 u32 chip;
 reset(); bool matched=false;
 for(unsigned int i=0;i<ARRAY_SIZE(target_compatibles);i++)
  for(unsigned int j=0;adreno_match_table[j].compatible;j++)
   if(!strcmp(target_compatibles[i],adreno_match_table[j].compatible)) { assert(adreno_match_table[j].data==&device_3d0); matched=true; }
 assert(matched);
 reset(); chip=0; assert(adreno_get_chipid(&pdev,&chip)==0 && chip==0x06080001 && !fuse_calls && !revision_calls && gpu_node.refs==1);
 reset(); gpu_node.chip_present=true; assert(adreno_get_chipid(&pdev,&chip)==0 && chip==0x06080001 && !fuse_calls);
 reset(); chip=123; gpu_node.chip_present=true; gpu_node.chip=0x06090000; assert(adreno_get_chipid(&pdev,&chip)==-EINVAL && chip==123 && !fuse_calls);
 reset(); chip=123; gpu_node.chip_present=true; property_error=-EOVERFLOW; assert(adreno_get_chipid(&pdev,&chip)==-EOVERFLOW && chip==123);
 reset(); gpu_node.native=false; gpu_node.chip_present=true; assert(adreno_get_chipid(&pdev,&chip)==0 && fuse_calls==1 && revision_calls==1 && gpu_node.refs==1);
 reset(); gpu_node.native=false; use_revision=true; assert(adreno_get_chipid(&pdev,&chip)==0 && !revision_node.refs && gpu_node.refs==1);
 reset(); gpu_node.native=false; assert(adreno_get_chipid(&pdev,&chip)==-EINVAL && gpu_node.refs==1);
 reset(); assert(adreno_identify_gpu(&pdev,&chip)==&gpucore);
 reset(); gpu_node.chip_present=true; property_error=-EOVERFLOW; assert(PTR_ERR(adreno_identify_gpu(&pdev,&chip))==-EOVERFLOW);
 reset(); no_core=true; assert(PTR_ERR(adreno_identify_gpu(&pdev,&chip))==-ENODEV);
 reset(); gpucore.features=ADRENO_DEPRECATED; assert(PTR_ERR(adreno_identify_gpu(&pdev,&chip))==-ENODEV);
 reset(); assert(adreno_bind_gmu(&kgsl)==0 && bind_calls==1 && !find_calls && kgsl.gmu_core.components_bound);
 assert(adreno_bind_gmu(&kgsl)==-EBUSY && bind_calls==1); adreno_unbind_gmu(&kgsl); assert(unbind_calls==1 && !kgsl.gmu_core.components_bound);
 adreno_unbind_gmu(&kgsl); assert(unbind_calls==1);
 reset(); bind_error=-EPROBE_DEFER; assert(adreno_bind_gmu(&kgsl)==-EPROBE_DEFER && !kgsl.gmu_core.components_bound);
 adreno_unbind_gmu(&kgsl); assert(!unbind_calls); bind_error=0; assert(adreno_bind_gmu(&kgsl)==0); adreno_unbind_gmu(&kgsl); assert(unbind_calls==1);
 reset(); gpu_node.gmu_ref=false; assert(adreno_bind_gmu(&kgsl)==-EINVAL && !find_calls && !bind_calls);
 reset(); gpu_node.native=gpu_node.gmu_ref=false; assert(adreno_bind_gmu(&kgsl)==0 && find_calls==1 && bind_calls==1 && !gmu_node.refs); adreno_unbind_gmu(&kgsl);
 reset(); gpu_node.native=gpu_node.gmu_ref=false; have_gmu=false; assert(adreno_bind_gmu(&kgsl)==0 && !bind_calls && !kgsl.gmu_core.components_bound); adreno_unbind_gmu(&kgsl); assert(!unbind_calls);
 reset(); assert(kgsl_request_gpu_irq(&kgsl)==703 && irq_lookup==1 && !irq_named && irq_requests==1);
 reset(); gpu_node.native=false; assert(kgsl_request_gpu_irq(&kgsl)==703 && !irq_lookup && irq_named==1 && irq_requests==1);
 reset(); irq_value=-EPROBE_DEFER; assert(kgsl_request_gpu_irq(&kgsl)==-EPROBE_DEFER && !irq_requests);
 reset(); irq_value=-ENXIO; assert(kgsl_request_gpu_irq(&kgsl)==-ENXIO && !irq_requests);
 reset(); irq_error=-EBUSY; assert(kgsl_request_gpu_irq(&kgsl)==-EBUSY && irq_requests==1);
 reset(); assert(kgsl_3d_init()==0 && core_live && driver_live[0] && driver_live[1] && driver_live[2]); kgsl_3d_exit(); EXPECT_TRACE(1,10,11,12,22,21,20,2);
 reset(); core_error=-ENOMEM; assert(kgsl_3d_init()==-ENOMEM && !core_exits); EXPECT_TRACE(1);
 reset(); driver_error[0]=-EBUSY; assert(kgsl_3d_init()==-EBUSY && core_exits==1); EXPECT_TRACE(1,10,2);
 reset(); driver_error[1]=-EIO; assert(kgsl_3d_init()==-EIO && core_exits==1); EXPECT_TRACE(1,10,11,20,2);
 reset(); driver_error[2]=-EINVAL; assert(kgsl_3d_init()==-EINVAL && core_exits==1); EXPECT_TRACE(1,10,11,12,21,20,2);
 unsigned int total=cases; reset(); printf("%u GPU probe scenarios passed (hardware/OF/component leaves mocked)\n",total); return 0;
}
