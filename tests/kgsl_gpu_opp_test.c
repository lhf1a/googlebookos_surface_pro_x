/* OPP API leaves mocked; actual KGSL loader, voltage conversion and HFI layout. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
typedef uint16_t u16; typedef uint32_t u32; typedef uint16_t __le16;
#define __packed __attribute__((packed))
#define U16_MAX UINT16_MAX
#define U32_MAX UINT32_MAX
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define EPROBE_DEFER 517
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p)>=(uintptr_t)-4095)
#define IS_ERR_OR_NULL(p) (!(p)||IS_ERR(p))
#define dev_err(...) ((void)0)
#define WARN_ON(x) (assert(!(x)),false)
#define SZ_1K 1024
#include "kgsl-gpu-opp-types.h"
struct dev_pm_opp { unsigned long freq; unsigned int level; };
#include "kgsl-gpu-opp-target.h"
struct device_node { bool native, opp_v2; int initial; };
struct device { struct device_node *of_node; };
struct platform_device { struct device dev; };
struct kgsl_pwrctrl {
 struct kgsl_pwrlevel pwrlevels[KGSL_MAX_PWRLEVELS];
 unsigned int num_pwrlevels, active_pwrlevel, default_pwrlevel;
};
struct kgsl_device { struct platform_device *pdev; struct kgsl_pwrctrl pwrctrl; unsigned int speed_bin; };
struct a6xx_gmu_device { struct platform_device *pdev; struct {struct hfi_dcvstable_cmd dcvs_table;} hfi; };
struct adreno_device { struct kgsl_device dev; struct a6xx_gmu_device gmu; };
#define KGSL_DEVICE(a) (&(a)->dev)
#define to_a6xx_gmu(a) (&(a)->gmu)
static struct device_node node;
static struct platform_device pdev;
static struct adreno_device adreno;
static struct dev_pm_opp opps[20];
static int count, add_error, find_error_at, count_error, additions, finds, puts_count, outstanding;
static int limit_calls, child_lookups, legacy_calls, sends, send_error;
static bool repeat_frequency;
static struct hfi_dcvstable_v1_cmd sent;
static unsigned int cases;
static bool of_property_present(struct device_node *n,const char *p) { assert(!strcmp(p,"operating-points-v2"));return n->opp_v2; }
static bool of_device_is_compatible(struct device_node *n,const char *p) { assert(!strcmp(p,"qcom,adreno-680.1"));return n->native; }
static int of_property_read_u32(struct device_node *n,const char *p,void *value)
{ assert(!strcmp(p,"qcom,initial-pwrlevel"));if(n->initial==-2)return -EINVAL;*(u32 *)value=n->initial;return 0; }
static struct device_node *of_get_child_by_name(struct device_node *n,const char *name) { child_lookups++;return NULL; }
#define for_each_child_of_node(n,c) for(c=NULL;c;c=NULL)
static void of_node_put(struct device_node *n) { assert(false); }
static int adreno_of_get_legacy_pwrlevels(struct adreno_device *a,struct device_node *n) { legacy_calls++;return -ENOENT; }
static int adreno_of_parse_pwrlevels(struct adreno_device *a,struct device_node *n) { assert(false);return -EINVAL; }
static void adreno_of_get_limits(struct adreno_device *a,struct device_node *n) { limit_calls++; }
static int devm_pm_opp_of_add_table(struct device *d) { assert(d==&pdev.dev);additions++;return add_error; }
static int dev_pm_opp_get_opp_count(struct device *d) { assert(d==&pdev.dev);return count_error ? count_error : count; }
static struct dev_pm_opp *dev_pm_opp_find_freq_floor(struct device *d,unsigned long *freq)
{
 assert(d==&pdev.dev && !outstanding);finds++;
 if(finds==find_error_at)return ERR_PTR(-EIO);
 if(repeat_frequency && finds==2){*freq=opps[count-1].freq;outstanding++;return &opps[count-1];}
 for(int i=count-1;i>=0;i--)if(opps[i].freq<=*freq){*freq=opps[i].freq;outstanding++;return &opps[i];}
 return ERR_PTR(-ERANGE);
}
static unsigned int dev_pm_opp_get_level(struct dev_pm_opp *opp) { assert(outstanding==1);return opp->level; }
static void dev_pm_opp_put(struct dev_pm_opp *opp) { assert(outstanding==1);outstanding--;puts_count++; }
static u16 get_unaligned_le16(const void *p) { const unsigned char *b=p;return b[0]|((u16)b[1]<<8); }
static const void *cmd_db_read_aux_data(const char *name,size_t *len)
{
 if(!strcmp(name,"gfx.lvl")){*len=sizeof(target_gfx);return target_gfx;}
 assert(!strcmp(name,"mx.lvl"));*len=sizeof(target_mx);return target_mx;
}
static int a6xx_hfi_send_generic_req(struct adreno_device *a,void *data) { assert(a==&adreno);sent=*(struct hfi_dcvstable_v1_cmd *)data;sends++;return send_error; }
#include "kgsl-gpu-opp-functions.h"
static void reset(void)
{
 assert(!outstanding);
 memset(&adreno,0,sizeof(adreno));memset(opps,0,sizeof(opps));memset(&sent,0,sizeof(sent));
 node=(struct device_node){.native=true,.opp_v2=true,.initial=-2};pdev.dev.of_node=&node;
 adreno.dev.pdev=&pdev;adreno.gmu.pdev=&pdev;
 /* Invalid prior state makes publication before validation visible. */
 memset(&adreno.dev.pwrctrl,0xa5,sizeof(adreno.dev.pwrctrl));
 memcpy(opps,target_opps,sizeof(target_opps));count=ARRAY_SIZE(target_opps);
 add_error=find_error_at=count_error=additions=finds=puts_count=limit_calls=child_lookups=legacy_calls=sends=send_error=0;
 repeat_frequency=false;cases++;
}
static void failure(int expected)
{
 struct kgsl_pwrctrl before=adreno.dev.pwrctrl;
 assert(adreno_of_get_pwrlevels(&adreno,&node)==expected);
 assert(!memcmp(&before,&adreno.dev.pwrctrl,sizeof(before)) && !outstanding && !limit_calls && !child_lookups && !legacy_calls);
}
int main(void)
{
 struct rpmh_arc_vals gx={0},mx={0};
 /* Independent expected values for the captured GFX/MX rails, ordered from off. */
 const u32 expected_votes[]={0,0x00400203,0x00800204,0x00c00305,0x01000406,0x01400507,0x01800608,0x01a00709,0x01a00709,0x01b0070a,0x01b0070a};
 reset();assert(count==10 && adreno_of_get_pwrlevels(&adreno,&node)==0);
 assert(additions==1 && finds==10 && puts_count==10 && !legacy_calls && !child_lookups && limit_calls==1);
 assert(adreno.dev.pwrctrl.num_pwrlevels==10 && adreno.dev.pwrctrl.default_pwrlevel==1 && adreno.dev.pwrctrl.active_pwrlevel==1);
 for(int i=0;i<10;i++){
  struct kgsl_pwrlevel *l=&adreno.dev.pwrctrl.pwrlevels[i];
  assert(l->gpu_freq==target_opps[9-i].freq && l->voltage_level==target_opps[9-i].level);
  assert(!l->bus_freq && !l->bus_min && !l->bus_max && !l->acd_level);
 }
 assert(adreno.dev.pwrctrl.pwrlevels[0].gpu_freq==700000000 && adreno.dev.pwrctrl.pwrlevels[9].gpu_freq==177000000);
 assert(!rpmh_arc_cmds(&gx,"gfx.lvl") && !rpmh_arc_cmds(&mx,"mx.lvl"));
 assert(!setup_gx_arc_votes(&adreno,&gx,&mx));adreno.gmu.hfi.dcvs_table.gmu_level_num=1;
 assert(!a6xx_hfi_send_dcvstbl_v1(&adreno) && sends==1 && sent.gpu_level_num==11);
 assert(sent.gx_votes[0].freq==0);
 for(int i=0;i<11;i++){assert(sent.gx_votes[i].vote==expected_votes[i]);if(i)assert(sent.gx_votes[i].freq==target_opps[i-1].freq/1000);}
 u32 fake_bw[]={0,1000};assert(validate_pwrlevels(&adreno.dev,fake_bw,2)==-EINVAL);
 reset();node.opp_v2=false;failure(-ENODEV);assert(!additions);
 reset();add_error=-EPROBE_DEFER;failure(-EPROBE_DEFER);assert(!finds);
 reset();add_error=-EINVAL;failure(-EINVAL);
 reset();count=0;failure(-ENODEV);
 reset();count_error=-EIO;failure(-EIO);
 reset();count=MAX_GX_LEVELS;failure(-E2BIG);assert(!finds);
 reset();count=KGSL_MAX_PWRLEVELS+1;failure(-E2BIG);assert(!finds);
 reset();find_error_at=1;failure(-EIO);assert(!puts_count);
 reset();find_error_at=4;failure(-EIO);assert(puts_count==3);
 reset();count=1;opps[0].freq=0;failure(-ERANGE);assert(puts_count==1);
 reset();opps[9].freq=(unsigned long)U32_MAX+1;failure(-ERANGE);assert(puts_count==1);
 reset();opps[9].freq++;failure(-ERANGE);
 reset();opps[5].level=0;failure(-ERANGE);assert(puts_count==5);
 reset();opps[9].level=(unsigned int)U16_MAX+1;failure(-ERANGE);
 reset();opps[9].level=U32_MAX;failure(-ERANGE);
 reset();repeat_frequency=true;failure(-ERANGE);assert(puts_count==2);
 reset();count=1;assert(!adreno_of_get_pwrlevels(&adreno,&node) && !adreno.dev.pwrctrl.default_pwrlevel && !adreno.dev.pwrctrl.active_pwrlevel);
 reset();node.initial=9;assert(!adreno_of_get_pwrlevels(&adreno,&node) && adreno.dev.pwrctrl.default_pwrlevel==9);
 reset();node.initial=10;assert(!adreno_of_get_pwrlevels(&adreno,&node) && adreno.dev.pwrctrl.default_pwrlevel==1);
 reset();node.initial=-1;assert(!adreno_of_get_pwrlevels(&adreno,&node) && adreno.dev.pwrctrl.default_pwrlevel==1);
 reset();count=1;node.initial=8;assert(!adreno_of_get_pwrlevels(&adreno,&node) && !adreno.dev.pwrctrl.default_pwrlevel);
 reset();count=MAX_GX_LEVELS-1;for(int i=0;i<count;i++)opps[i]=(struct dev_pm_opp){(i+1)*1000000,64};assert(!adreno_of_get_pwrlevels(&adreno,&node));assert(!setup_gx_arc_votes(&adreno,&gx,&mx) && adreno.gmu.hfi.dcvs_table.gpu_level_num==MAX_GX_LEVELS);
 reset();node.native=false;assert(adreno_of_get_pwrlevels(&adreno,&node)==-ENOENT && !additions && legacy_calls==1);
 reset();assert(!adreno_of_get_pwrlevels(&adreno,&node));assert(!setup_gx_arc_votes(&adreno,&gx,&mx));adreno.gmu.hfi.dcvs_table.gmu_level_num=1;send_error=-ETIMEDOUT;assert(a6xx_hfi_send_dcvstbl_v1(&adreno)==-ETIMEDOUT);
 unsigned int total=cases;reset();printf("%u GPU OPP/ARC/HFI scenarios passed (OPP API and firmware mocked)\n",total);return 0;
}
