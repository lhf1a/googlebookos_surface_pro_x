/* Actual lookup functions, reference-counted fake OF trees, and early probe slice. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define EPROBE_DEFER 517
#define dev_err(...) ((void)0)
#define dev_warn(...) ((void)0)
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define IS_ERR(p) ((uintptr_t)(p)>=(uintptr_t)-4095)
#define ANY_ID 255
#define MAX_L3_LEVELS 4
#define ADRENO_CHIPID_CORE(c) ((c)>>24)
#define ADRENO_CHIPID_MAJOR(c) (((c)>>16)&255)
#define ADRENO_CHIPID_MINOR(c) (((c)>>8)&255)
#define ADRENO_CHIPID_PATCH(c) ((c)&255)
typedef uint32_t u32;
struct device_node {
 const char *name, *key, *compatible; u32 value, freq; int refs;
 bool available; struct device_node *child, *next;
};
struct device { struct device_node *of_node; void *data; };
struct platform_device { struct device dev; };
struct kgsl_pwrctrl { int unused; };
struct kgsl_device {
 struct platform_device *pdev; struct kgsl_pwrctrl pwrctrl; int speed_bin, num_l3_pwrlevels, context_idr;
 u32 l3_freq[MAX_L3_LEVELS]; void *l3_icc;
};
struct adreno_device { struct kgsl_device dev; };
#define KGSL_DEVICE(a) (&(a)->dev)
struct adreno_gpu_core { const char *compatible; unsigned int core,major,minor,patchid; };
static const struct adreno_gpu_core chip_core={NULL,6,8,0,ANY_ID}, model_core={"test,model",6,8,0,1};
static const struct adreno_gpu_core *adreno_gpulist[]={&chip_core,&model_core};
static struct device_node nodes[9];
#define root nodes[0]
#define group nodes[1]
#define first nodes[2]
#define second nodes[3]
#define foreign_root nodes[4]
#define foreign_group nodes[5]
static struct platform_device pdev;
static struct adreno_device adreno;
static int fuse, parse_error, parse_calls, limit_calls, initial_calls, icc_calls;
static int speed_error, power_error, bus_error, setup_calls, power_calls, bus_calls, unbind_calls, close_calls, idr_destroys;
static struct device_node *parsed_node;
static bool idr_live;
static unsigned int cases;
static struct device_node *of_node_get(struct device_node *n) { if(n)n->refs++; return n; }
static void of_node_put(struct device_node *n) { if(n){ assert(n->refs>1); n->refs--; } }
static struct device_node *get_next(const struct device_node *parent, struct device_node *prev, bool available)
{
 struct device_node *next=prev ? prev->next : parent->child;
 of_node_put(prev);
 while(next && available && !next->available)next=next->next;
 return of_node_get(next);
}
#define for_each_child_of_node(p,c) for(c=get_next(p,NULL,false);c;c=get_next(p,c,false))
#define for_each_available_child_of_node(p,c) for(c=get_next(p,NULL,true);c;c=get_next(p,c,true))
static bool of_node_name_eq(const struct device_node *n,const char *name) { return !strcmp(n->name,name); }
static int of_property_read_u32(struct device_node *n,const char *name,u32 *v)
{
 if(!strcmp(name,"qcom,l3-freq")){*v=n->freq;return 0;}
 if(!n->key || strcmp(name,n->key))return -EINVAL;
 *v=n->value; return 0;
}
static void *of_find_property(struct device_node *n,const char *name,void *len)
{ assert(!strcmp(name,"compatible") && !len);return n && n->compatible ? n : NULL; }
static bool of_device_is_compatible(struct device_node *n,const char *name)
{ return n && n->compatible && !strcmp(n->compatible,name); }
static int adreno_read_gpu_model_fuse(struct platform_device *p) { assert(p==&pdev);return fuse; }
static int adreno_of_parse_pwrlevels(struct adreno_device *a,struct device_node *n)
{ assert(a==&adreno);parse_calls++;parsed_node=n;return parse_error; }
static void adreno_of_get_initial_pwrlevel(struct kgsl_pwrctrl *p,struct device_node *n)
{ assert(p==&adreno.dev.pwrctrl);initial_calls++; }
static void adreno_of_get_limits(struct adreno_device *a,struct device_node *n) { assert(a==&adreno);limit_calls++; }
static void *of_icc_get(struct device *d,const char *name)
{ assert(d==&pdev.dev && !strcmp(name,"l3_path"));icc_calls++;return &pdev; }
static void adreno_setup_device(struct adreno_device *a) { assert(a==&adreno && !idr_live);setup_calls++;idr_live=true; }
static void dev_set_drvdata(struct device *d,void *p) { d->data=p; }
static void adreno_update_soc_hw_revision_quirks(struct adreno_device *a,struct platform_device *p) {}
static int adreno_read_speed_bin(struct platform_device *p) { return speed_error; }
static int adreno_of_get_power(struct adreno_device *a,struct platform_device *p) { power_calls++;return power_error; }
static int kgsl_bus_init(struct kgsl_device *d,struct platform_device *p) { bus_calls++;return bus_error; }
static void adreno_unbind_gmu(struct kgsl_device *d) { assert(d->pdev==&pdev);unbind_calls++; }
static void idr_destroy(int *p) { assert(p==&adreno.dev.context_idr && idr_live);idr_live=false;idr_destroys++; }
static void kgsl_bus_close(struct kgsl_device *d) { assert(d==&adreno.dev && d->pdev==&pdev);close_calls++; }
/* Kept only to make negative controls reproduce the borrowed-root failure. */
static __attribute__((unused)) struct device_node *of_find_node_by_name(struct device_node *from,const char *name)
{ assert(!"global OF search consumes a borrowed root"); return NULL; }
static int adreno_of_get_native_pwrlevels(struct adreno_device *a) { assert(!"native branch is covered by GPU OPP tests"); return -EINVAL; }
#include "kgsl-dt-ownership-functions.h"
static void reset(void)
{
 if(cases) for(unsigned int i=0;i<ARRAY_SIZE(nodes);i++)assert(nodes[i].refs==1);
 assert(!idr_live);
 memset(nodes,0,sizeof(nodes));
 for(unsigned int i=0;i<ARRAY_SIZE(nodes);i++){nodes[i].refs=1;nodes[i].available=true;nodes[i].name="node";}
 root.name="gpu"; root.next=&foreign_root; foreign_root.name="other-gpu"; foreign_root.child=&foreign_group;
 foreign_group.name="qcom,gpu-pwrlevels";
 pdev=(struct platform_device){.dev.of_node=&root};adreno=(struct adreno_device){.dev.pdev=&pdev};
 fuse=3;parse_error=parse_calls=limit_calls=initial_calls=icc_calls=0;parsed_node=NULL;
 speed_error=power_error=bus_error=setup_calls=power_calls=bus_calls=unbind_calls=close_calls=idr_destroys=0;
 cases++;
}
static void tree(const char *name,const char *key,u32 value)
{ root.child=&group;group.name=name;group.child=&first;first.key=key;first.value=value; }
static void early_failure(int error,int power,int bus)
{
 assert(adreno_probe_prefix_test(&pdev,&adreno)==error);
 assert(setup_calls==1 && power_calls==power && bus_calls==bus);
 assert(!idr_live && idr_destroys==1 && close_calls==1 && unbind_calls==1);
 assert(!pdev.dev.data && !adreno.dev.pdev);
}
int main(void)
{
 struct device_node *n;
 reset();assert(!adreno_get_soc_hw_revision_node(&pdev,7));
 reset();tree("qcom,soc-hw-revisions","qcom,soc-hw-revision",7);n=adreno_get_soc_hw_revision_node(&pdev,7);assert(n==&first && first.refs==2);of_node_put(n);
 reset();tree("qcom,soc-hw-revisions",NULL,7);first.next=&second;second.key="qcom,soc-hw-revision";second.value=9;assert(!adreno_get_soc_hw_revision_node(&pdev,7));
 reset();assert(!adreno_get_gpu_model_node(&pdev));
 reset();fuse=-ENOENT;tree("qcom,gpu-models","qcom,gpu-model-id",3);assert(!adreno_get_gpu_model_node(&pdev));
 reset();tree("qcom,gpu-models","qcom,gpu-model-id",3);n=adreno_get_gpu_model_node(&pdev);assert(n==&first && first.refs==2);of_node_put(n);
 reset();tree("qcom,gpu-models","qcom,gpu-model-id",4);assert(!adreno_get_gpu_model_node(&pdev));
 reset();assert(_get_gpu_core(&pdev,0x06080001)==&chip_core);
 reset();root.compatible="test,model";assert(_get_gpu_core(&pdev,0x06080001)==&model_core);
 reset();tree("qcom,gpu-models","qcom,gpu-model-id",3);first.compatible="test,model";assert(_get_gpu_core(&pdev,0x06080001)==&model_core);
 reset();tree("qcom,gpu-models","qcom,gpu-model-id",3);root.compatible="test,model";assert(_get_gpu_core(&pdev,0x06080001)==&model_core);
 reset();tree("qcom,gpu-models","qcom,gpu-model-id",3);first.compatible="test,unknown";assert(_get_gpu_core(&pdev,0x06080001)==&chip_core);
 reset();tree("qcom,gpu-models","qcom,gpu-model-id",3);first.compatible="test,unknown";assert(!_get_gpu_core(&pdev,0x01000000));
 reset();assert(adreno_of_get_pwrlevels(&adreno,&root)==-EINVAL && !parse_calls);
 reset();tree("qcom,gpu-pwrlevels",NULL,0);assert(adreno_of_get_pwrlevels(&adreno,&root)==0 && parsed_node==&group && initial_calls==1 && limit_calls==1);
 reset();tree("qcom,gpu-pwrlevels",NULL,0);parse_error=-EIO;assert(adreno_of_get_pwrlevels(&adreno,&root)==-EIO && !initial_calls);
 reset();tree("qcom,gpu-pwrlevel-bins","qcom,speed-bin",0);assert(adreno_of_get_pwrlevels(&adreno,&root)==0 && parsed_node==&first && initial_calls==1 && limit_calls==2);
 reset();tree("qcom,gpu-pwrlevel-bins","qcom,speed-bin",0);parse_error=-ENOMEM;assert(adreno_of_get_pwrlevels(&adreno,&root)==-ENOMEM && !initial_calls);
 reset();tree("qcom,gpu-pwrlevel-bins","qcom,speed-bin",8);assert(adreno_of_get_pwrlevels(&adreno,&root)==-ENODEV && !parse_calls);
 reset();tree("qcom,gpu-pwrlevel-bins",NULL,0);assert(adreno_of_get_pwrlevels(&adreno,&root)==-ENODEV);
 reset();l3_pwrlevel_probe(&adreno.dev,&root);assert(!icc_calls);
 reset();tree("qcom,l3-pwrlevels","reg",1);first.freq=400;first.next=&second;second.key="reg";second.value=8;l3_pwrlevel_probe(&adreno.dev,&root);assert(icc_calls==1 && adreno.dev.num_l3_pwrlevels==2 && adreno.dev.l3_freq[1]==400);
 reset();tree("qcom,l3-pwrlevels",NULL,1);l3_pwrlevel_probe(&adreno.dev,&root);assert(!icc_calls);
 reset();tree("qcom,l3-pwrlevels",NULL,1);first.available=false;l3_pwrlevel_probe(&adreno.dev,&root);assert(icc_calls==1 && adreno.dev.num_l3_pwrlevels==0);
 reset();speed_error=-EPROBE_DEFER;early_failure(-EPROBE_DEFER,0,0);
 reset();power_error=-EINVAL;early_failure(-EINVAL,1,0);
 reset();bus_error=-ENODEV;early_failure(-ENODEV,1,1);
 reset();assert(adreno_probe_prefix_test(&pdev,&adreno)==0 && idr_live && pdev.dev.data==&adreno.dev && adreno.dev.pdev==&pdev && !close_calls);
 idr_destroy(&adreno.dev.context_idr);pdev.dev.data=NULL;
 unsigned int total=cases;reset();printf("%u DT ownership/early-probe scenarios passed (OF leaves and resource APIs mocked)\n",total);return 0;
}
