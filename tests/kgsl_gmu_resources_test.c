/* Kernel/platform/IRQ/MMIO mocks; tested functions are extracted unchanged. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define EPROBE_DEFER 517
#define U32_MAX UINT32_MAX
#define __iomem
#define IORESOURCE_MEM 1
#define IRQF_TRIGGER_HIGH 4
#define IRQF_NO_AUTOEN 0x80000
#define dev_err(...) ((void)0)
typedef uint32_t u32;
typedef uint64_t resource_size_t;
typedef int (*irq_handler_t)(int, void *);
struct resource { resource_size_t start, end; const char *name; };
#define resource_size(r) ((r)->end - (r)->start + 1)
struct device_node { bool has_ref, native, supported, available; int refs; };
struct device { struct device_node *of_node; };
struct platform_device { struct device dev; };
struct component_match { int unused; };
struct kgsl_device {
 resource_size_t reg_phys;
 struct { u32 gmu2gpu_offset, reg_len; void *reg_virt; } gmu_core;
};
struct a6xx_gmu_device { struct platform_device *pdev; };
struct adreno_a6xx_core { bool pdc_in_aop; };
struct adreno_device { struct kgsl_device device; struct a6xx_gmu_device gmu; struct adreno_a6xx_core core; bool native; };
#define KGSL_DEVICE(d) (&(d)->device)
#define to_a6xx_gmu(d) (&(d)->gmu)
#define to_a6xx_core(d) (&(d)->core)
#define adreno_is_a680(d) ((d)->native)
#define adreno_is_a640v2(d) false
#define adreno_is_a620(d) false
#define adreno_is_a618(d) false
#define adreno_is_a619(d) false
#define adreno_is_a650_family(d) false
#include "kgsl-gmu-resources-target.h"
static struct resource resources[3];
static bool absent[3];
static struct device_node gpu_node, gmu_node;
static struct platform_device pdev, gpu_pdev;
static struct adreno_device adreno;
static struct component_match match_object;
static const int adreno_gmu_match[1], adreno_ops;
static int map_calls, fail_map, unmaps, writes, barriers, request_calls, irq_value, irq_error;
static int missing_cmd, find_calls, match_calls, master_calls, bind_calls, del_calls, unbind_calls;
static int component_error, bind_error;
static bool bad_ref;
static char irq_name[32];
static u32 cfg_words[0x4000], seq_words[0x4000];
static bool cfg_live, seq_live;
static unsigned int cases;
static void *handler_data;
static int handler(int irq, void *data) { assert(!"IRQ must not fire during request"); return 0; }
static struct resource *platform_get_resource_byname(struct platform_device *p, int type, const char *name)
{
 assert(p == &pdev && type == IORESOURCE_MEM);
 for (int i=0;i<3;i++) if (!absent[i] && !strcmp(name, resources[i].name)) return &resources[i];
 return NULL;
}
static void *devm_ioremap(struct device *dev, resource_size_t start, resource_size_t len)
{
 assert(dev == &pdev.dev && start == resources[0].start && len == resource_size(&resources[0]));
 return ++map_calls == fail_map ? NULL : cfg_words;
}
static void *ioremap(resource_size_t start, resource_size_t len)
{
 int i = start == resources[1].start ? 1 : 2;
 assert(start == resources[i].start && len == resource_size(&resources[i]));
 if (++map_calls == fail_map) return NULL;
 if (i == 1) { assert(!cfg_live); cfg_live=true; return cfg_words; }
 assert(!seq_live); seq_live=true; return seq_words;
}
static void iounmap(void *ptr)
{
 if (ptr == cfg_words) { assert(cfg_live); cfg_live=false; }
 else { assert(ptr == seq_words && seq_live); seq_live=false; }
 unmaps++;
}
static void _regwrite(void *ptr, u32 offset, u32 value)
{
 int i = ptr == cfg_words ? 1 : 2;
 assert(ptr == cfg_words ? cfg_live : ptr == seq_words && seq_live);
 assert((uint64_t)offset * 4 + 4 <= resource_size(&resources[i]));
 ((u32 *)ptr)[offset]=value; writes++;
}
static void wmb(void) { barriers++; }
static u32 cmd_db_read_addr(const char *name)
{
 const char *names[] = {"vrm.soc", "xo.lvl", "cx.lvl", "mx.lvl"};
 for (int i=0;i<4;i++) if (!strcmp(name,names[i])) return i == missing_cmd ? 0 : 0x12000 + i*0x100;
 assert(false); return 0;
}
static int platform_get_irq_byname(struct platform_device *p, const char *name)
{
 assert(p == &pdev); snprintf(irq_name,sizeof(irq_name),"%s",name); return irq_value;
}
static int devm_request_irq(struct device *dev, int irq, irq_handler_t fn, unsigned long flags, const char *name, void *data)
{
 assert(dev == &pdev.dev && irq == irq_value && fn == handler && data == handler_data);
 assert(!strcmp(name,irq_name)); request_calls++;
 if (!(flags & IRQF_NO_AUTOEN)) fn(irq,data);
 assert(flags == (IRQF_TRIGGER_HIGH | IRQF_NO_AUTOEN)); return irq_error;
}
static bool of_property_present(struct device_node *n, const char *p) { assert(!strcmp(p,"qcom,gmu")); return n->has_ref; }
static struct device_node *of_parse_phandle(struct device_node *n, const char *p, int index)
{
 assert(n==&gpu_node && !strcmp(p,"qcom,gmu") && index==0);
 if (bad_ref) return NULL;
 gmu_node.refs++; return &gmu_node;
}
static bool of_device_is_compatible(struct device_node *n, const char *c) { assert(!strcmp(c,"qcom,adreno-680.1")); return n->native; }
static const void *of_match_node(const void *table, struct device_node *n) { assert(table==&adreno_gmu_match); return n->supported ? table : NULL; }
static struct device_node *of_find_matching_node(void *from, const void *table)
{
 assert(!from && table==&adreno_gmu_match); find_calls++;
 if (!gmu_node.supported) return NULL;
 gmu_node.refs++; return &gmu_node;
}
static bool of_device_is_available(struct device_node *n) { return n->available; }
static void of_node_put(void *p) { struct device_node *n=p; assert(n->refs>0); n->refs--; }
static void (*release_match)(struct device *, void *);
static void *match_data;
static void component_match_add_release(struct device *dev, struct component_match **m,
 void (*release)(struct device *, void *), int (*compare)(struct device *, void *), void *data)
{
 assert(dev==&gpu_pdev.dev && compare(&pdev.dev,data));
 match_calls++; release_match=release; match_data=data; *m=&match_object;
}
static int component_master_add_with_match(struct device *dev, const void *ops, struct component_match *match)
{ assert(dev==&gpu_pdev.dev && ops==&adreno_ops && match==&match_object); master_calls++; return component_error; }
static int adreno_bind(struct device *dev) { assert(dev==&gpu_pdev.dev); bind_calls++; return bind_error; }
static void component_master_del(struct device *dev, const void *ops)
{ assert(dev==&gpu_pdev.dev && ops==&adreno_ops); del_calls++; }
static void adreno_unbind(struct device *dev) { assert(dev==&gpu_pdev.dev); unbind_calls++; }
#include "kgsl-gmu-resources-functions.h"
static void reset(void)
{
 if (release_match) { release_match(&gpu_pdev.dev,match_data); release_match=NULL; }
 assert(!cfg_live && !seq_live && !gmu_node.refs);
 memcpy(resources,target_resources,sizeof(resources)); memset(absent,0,sizeof(absent));
 memset(cfg_words,0,sizeof(cfg_words)); memset(seq_words,0,sizeof(seq_words));
 gpu_node=(struct device_node){.has_ref=true,.native=true};
 gmu_node=(struct device_node){.supported=true,.available=true};
 pdev.dev.of_node=&gmu_node; gpu_pdev.dev.of_node=&gpu_node;
 adreno=(struct adreno_device){.native=true,.gmu.pdev=&pdev,.device.reg_phys=TARGET_GPU_BASE};
 adreno.device.gmu_core.reg_virt=(void *)0x1234;
 adreno.device.gmu_core.reg_len=7; adreno.device.gmu_core.gmu2gpu_offset=9;
 map_calls=fail_map=unmaps=writes=barriers=request_calls=irq_error=0; irq_value=607;
 find_calls=match_calls=master_calls=bind_calls=del_calls=unbind_calls=0;
 component_error=bind_error=0; bad_ref=false; missing_cmd=-1; handler_data=&adreno;
 cases++;
}
static void mapping_error(int expected)
{
 assert(a6xx_gmu_reg_probe(&adreno)==expected);
 assert(adreno.device.gmu_core.reg_virt==(void *)0x1234);
 assert(adreno.device.gmu_core.reg_len==7 && adreno.device.gmu_core.gmu2gpu_offset==9);
}
int main(void)
{
 reset(); assert(a6xx_gmu_reg_probe(&adreno)==0);
 assert(adreno.device.gmu_core.reg_virt==cfg_words);
 assert(adreno.device.gmu_core.reg_len==0x30000 && adreno.device.gmu_core.gmu2gpu_offset==0x1a800);
 reset(); absent[0]=true; mapping_error(-ENODEV); assert(!map_calls);
 reset(); resources[0].end=resources[0].start-1; mapping_error(-EINVAL);
 reset(); resources[0].start++; mapping_error(-EINVAL);
 reset(); resources[0].end--; mapping_error(-EINVAL);
 reset(); resources[0].end=resources[0].start+0x22fff; mapping_error(-EINVAL);
 reset(); adreno.device.reg_phys=resources[0].start+4; mapping_error(-EINVAL);
 reset(); adreno.device.reg_phys++; mapping_error(-EINVAL);
 reset(); resources[0].end=resources[0].start+0x100000003ULL; mapping_error(-ERANGE);
 reset(); resources[0].start=0x1000000000ULL; resources[0].end=resources[0].start+0x2ffff; mapping_error(-ERANGE);
 reset(); fail_map=1; mapping_error(-ENOMEM); assert(map_calls==1);
 reset(); adreno.native=false; resources[0].name="kgsl_gmu_reg"; assert(a6xx_gmu_reg_probe(&adreno)==0);
 for (int legacy=0;legacy<2;legacy++) for(int hfi=0;hfi<2;hfi++) {
  reset(); adreno.native=!legacy; assert(a6xx_gmu_request_irq(&adreno,hfi,handler,handler_data)==607);
  assert(request_calls==1 && !strcmp(irq_name, legacy ? (hfi ? "kgsl_hfi_irq":"kgsl_gmu_irq") : (hfi ? "hfi":"gmu")));
 }
 reset(); irq_value=-EPROBE_DEFER; assert(a6xx_gmu_request_irq(&adreno,0,handler,handler_data)==-EPROBE_DEFER && !request_calls);
 reset(); irq_error=-EBUSY; assert(a6xx_gmu_request_irq(&adreno,1,handler,handler_data)==-EBUSY);
 reset(); assert(a6xx_load_pdc_ucode(&adreno)==0); assert(unmaps==2 && barriers==1 && writes==31);
 assert(seq_words[0]==0xfebea1e1 && seq_words[4]==0x002081fc);
 assert(cfg_words[PDC_GPU_TCS1_CMD0_ADDR]==cmd_db_read_addr("mx.lvl"));
 assert(cfg_words[PDC_GPU_TCS3_CMD0_ADDR+4]==cmd_db_read_addr("cx.lvl"));
 assert(cfg_words[PDC_GPU_TCS3_CMD0_ADDR+8]==cmd_db_read_addr("xo.lvl"));
 assert(cfg_words[PDC_GPU_TCS3_CMD0_DATA+4]==3 && cfg_words[PDC_GPU_ENABLE_PDC]==0x80000001);
 for (int i=1;i<4;i++) { reset(); missing_cmd=i; assert(a6xx_load_pdc_ucode(&adreno)==-ENOENT && !map_calls && !writes); }
 reset(); absent[1]=true; assert(a6xx_load_pdc_ucode(&adreno)==-ENODEV && !map_calls);
 reset(); resources[1].end=resources[1].start+0x100; assert(a6xx_load_pdc_ucode(&adreno)==-EINVAL && !map_calls);
 reset(); fail_map=1; assert(a6xx_load_pdc_ucode(&adreno)==-ENOMEM && !unmaps && !writes);
 reset(); absent[2]=true; assert(a6xx_load_pdc_ucode(&adreno)==-ENODEV && unmaps==1 && !writes);
 reset(); resources[2].end=resources[2].start+15; assert(a6xx_load_pdc_ucode(&adreno)==-EINVAL && unmaps==1 && !writes);
 reset(); fail_map=2; assert(a6xx_load_pdc_ucode(&adreno)==-ENOMEM && unmaps==1 && !writes);
 reset(); assert(adreno_probe(&gpu_pdev)==0 && match_calls==1 && master_calls==1 && !bind_calls && !find_calls && gmu_node.refs==1);
 adreno_remove(&gpu_pdev); assert(del_calls==1 && !unbind_calls && !find_calls);
 reset(); bad_ref=true; assert(adreno_probe(&gpu_pdev)==-EINVAL && !master_calls && !bind_calls && !find_calls);
 reset(); gmu_node.supported=false; assert(adreno_probe(&gpu_pdev)==-ENODEV && !gmu_node.refs && !master_calls && !bind_calls);
 reset(); gmu_node.available=false; assert(adreno_probe(&gpu_pdev)==-ENODEV && !gmu_node.refs && !master_calls && !bind_calls);
 reset(); gpu_node.has_ref=false; assert(adreno_probe(&gpu_pdev)==-EINVAL && !find_calls && !bind_calls);
 reset(); component_error=-ENOMEM; assert(adreno_probe(&gpu_pdev)==-ENOMEM && master_calls==1 && !bind_calls);
 reset(); gpu_node.has_ref=gpu_node.native=false; assert(adreno_probe(&gpu_pdev)==0 && find_calls==1 && master_calls==1);
 adreno_remove(&gpu_pdev); assert(del_calls==1 && gmu_node.refs==1);
 reset(); gpu_node.has_ref=gpu_node.native=false; gmu_node.supported=false; bind_error=-EIO;
 assert(adreno_probe(&gpu_pdev)==-EIO && bind_calls==1 && !master_calls);
 adreno_remove(&gpu_pdev); assert(unbind_calls==1 && !del_calls);
 unsigned int total=cases; reset(); printf("%u GMU resource scenarios passed (kernel APIs/MMIO mocked)\n",total);
 return 0;
}
