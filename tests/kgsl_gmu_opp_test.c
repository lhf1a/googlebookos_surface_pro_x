#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
typedef uint16_t u16;typedef uint32_t u32;
#define U16_MAX UINT16_MAX
#define U32_MAX UINT32_MAX
#define EPROBE_DEFER 517
#define GMU_FREQUENCY 200000000UL
#define KGSL_STATE_AWARE 7
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define IS_ENABLED(c) 0
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p)>=(uintptr_t)-4095)
#define IS_ERR_OR_NULL(p) (!(p) || IS_ERR(p))
typedef uint16_t __le16;
#define __packed __attribute__((packed))
#define WARN_ON(c) (assert(!(c)), false)
#define SZ_1K 1024
#include "kgsl-gmu-opp-types.h"
#include "kgsl-gmu-opp-target.h"
static u16 get_unaligned_le16(const void *p) {const unsigned char *b=p;return b[0]|((u16)b[1]<<8);}
static const void *cmd_db_read_aux_data(const char *name,size_t *len) {
 if(!strcmp(name,"cx.lvl")){*len=sizeof(target_cx);return target_cx;}
 assert(!strcmp(name,"mx.lvl"));*len=sizeof(target_mx);return target_mx;
}
struct device {void *of_node;};
struct platform_device {struct device dev;};
struct clk {unsigned long rate;};
struct clk_bulk_data {const char *id;struct clk *clk;};
struct a6xx_gmu_device {bool clks_on;struct platform_device *pdev;struct clk_bulk_data *clks;struct clk *core_clk,*hub_clk;int num_clks;struct {struct hfi_dcvstable_cmd dcvs_table;} hfi;};
struct kgsl_device {int state;};
struct adreno_device {struct kgsl_device dev;struct a6xx_gmu_device gmu;bool target;};
#define KGSL_DEVICE(a) (&(a)->dev)
static struct a6xx_gmu_device *to_a6xx_gmu(struct adreno_device *a) {return &a->gmu;}
static bool adreno_is_a680(struct adreno_device *a) {return a->target;}
struct dev_pm_opp {unsigned long freq;unsigned level;};
static struct platform_device pdev;
static struct clk clocks[6];
static struct clk_bulk_data bulk[6];
static struct dev_pm_opp opps[3];
static int bulk_count,add_error,opp_count,find_error_at,set_error,bulk_error,send_error;
static bool compatible,rounded;
static unsigned additions,finds,puts_count,sets,enables,rdpm,sends;
static struct hfi_dcvstable_v1_cmd sent;
static int devm_clk_bulk_get_all(struct device *d,struct clk_bulk_data **out) {*out=bulk;return bulk_count;}
static bool of_device_is_compatible(void *node,const char *name) {assert(!strcmp(name,"qcom,adreno-gmu"));return compatible;}
static struct clk *kgsl_of_clk_by_name(struct clk_bulk_data *list,int count,const char *name) {for(int i=0;i<count;++i)if(!strcmp(list[i].id,name))return list[i].clk;return NULL;}
static int devm_pm_opp_of_add_table(struct device *d) {++additions;return add_error;}
static int dev_pm_opp_get_opp_count(struct device *d) {return opp_count;}
static struct dev_pm_opp *dev_pm_opp_find_freq_ceil(struct device *d,unsigned long *freq) {++finds;if((int)finds==find_error_at)return ERR_PTR(-EIO);for(int i=0;i<opp_count;++i)if(opps[i].freq>=*freq){*freq=opps[i].freq;return &opps[i];}return ERR_PTR(-ERANGE);}
static unsigned dev_pm_opp_get_level(struct dev_pm_opp *o) {return o->level;}
static void dev_pm_opp_put(struct dev_pm_opp *o) {++puts_count;}
static int clk_set_rate(struct clk *c,unsigned long rate) {++sets;if(set_error)return set_error;c->rate=rate+rounded;return 0;}
static unsigned long clk_get_rate(struct clk *c) {return c->rate;}
static int clk_bulk_prepare_enable(int count,struct clk_bulk_data *clks) {++enables;return bulk_error;}
static void a6xx_rdpm_cx_freq_update(struct a6xx_gmu_device *g,u32 freq) {assert(freq==200000 && enables && !bulk_error);++rdpm;}
static int a6xx_hfi_send_generic_req(struct adreno_device *a,void *cmd) {++sends;sent=*(struct hfi_dcvstable_v1_cmd *)cmd;return send_error;}
#include "kgsl-gmu-opp-functions.h"
static struct adreno_device reset(void) {
 const char *names[]={"ahb","gmu","cxo","axi","memnoc","hub_clk"};
 for(int i=0;i<6;++i){bulk[i]=(struct clk_bulk_data){names[i],&clocks[i]};clocks[i].rate=1;}
 opps[0]=(struct dev_pm_opp){200000000,48};opps[1]=(struct dev_pm_opp){500000000,192};opps[2]=(struct dev_pm_opp){600000000,256};
 bulk_count=5;opp_count=2;add_error=find_error_at=set_error=bulk_error=send_error=0;compatible=true;rounded=false;
 additions=finds=puts_count=sets=enables=rdpm=sends=0;memset(&sent,0,sizeof(sent));
 return (struct adreno_device){.dev={.state=2},.gmu={.pdev=&pdev},.target=true};
}
int main(void) {
 unsigned cases=0;struct rpmh_arc_vals cx={0},mx={0};
 assert(!rpmh_arc_cmds(&cx,"cx.lvl") && !rpmh_arc_cmds(&mx,"mx.lvl"));
 /* Independent expected encodings for captured CX/MX ARC rails, including off. */
 const u32 expected_votes[]={0,0x00400202,0x00c00304,0x01000405};
 for(int scenario=0;scenario<10;++scenario) {struct adreno_device a=reset();int want=0;
  if(scenario==1){bulk_count=-EPROBE_DEFER;want=-EPROBE_DEFER;}
  if(scenario==2){bulk_count=0;want=-ENODEV;}
  if(scenario==3){compatible=false;want=-EOPNOTSUPP;}
  if(scenario>=4 && scenario<=8){bulk[scenario-4].clk=NULL;want=-ENODEV;}
  if(scenario==9){add_error=-EIO;want=-EIO;}
  assert(a6xx_gmu_clocks_probe(&a)==want);
  if(want)assert(!a.gmu.core_clk && !a.gmu.num_clks);
  else assert(a.gmu.core_clk==&clocks[1] && !a.gmu.hub_clk && a.gmu.num_clks==5 && additions==1);
  ++cases;
 }
 for(int scenario=0;scenario<11;++scenario) {struct adreno_device a=reset();a.gmu.hfi.dcvs_table.gmu_level_num=99;struct hfi_dcvstable_cmd before=a.gmu.hfi.dcvs_table;int want=0;
  if(scenario==1){opp_count=0;want=-ENODEV;}
  if(scenario==2){opp_count=-EIO;want=-EIO;}
  if(scenario==3){opp_count=4;want=-E2BIG;}
  if(scenario==4){find_error_at=2;want=-EIO;}
  if(scenario==5){opps[1].level=0;want=-ERANGE;}
  if(scenario==6){opps[1].level=65536;want=-ERANGE;}
  if(scenario==7){opps[1].freq=500000001;want=-ERANGE;}
  if(scenario==8){opps[1].freq=((unsigned long)UINT32_MAX+1)*1000;want=-ERANGE;}
  if(scenario==9){cx.num=0;want=-EINVAL;}
  if(scenario==10)opp_count=3;
  assert(setup_cx_arc_votes(&a.gmu,&cx,&mx)==want);
  assert(puts_count==finds-(find_error_at==2?1:0));
  assert(!rpmh_arc_cmds(&cx,"cx.lvl"));
  if(want)assert(!memcmp(&before,&a.gmu.hfi.dcvs_table,sizeof(before)));
  else {
   assert(a.gmu.hfi.dcvs_table.gmu_level_num==(unsigned)opp_count+1);
   for(int i=0;i<=opp_count;++i){assert(a.gmu.hfi.dcvs_table.cx_votes[i].freq==(i?opps[i-1].freq/1000:0));assert(a.gmu.hfi.dcvs_table.cx_votes[i].vote==expected_votes[i]);}
  }
  ++cases;
 }
 for(int scenario=0;scenario<9;++scenario) {struct adreno_device a=reset();assert(!a6xx_gmu_clocks_probe(&a));assert(!setup_cx_arc_votes(&a.gmu,&cx,&mx));int want=0;
  if(scenario==1){a.gmu.core_clk=NULL;want=-ENODEV;}
  if(scenario==2){a.gmu.num_clks=0;want=-ENODEV;}
  if(scenario==3){a.gmu.hfi.dcvs_table.gmu_level_num=5;want=-EINVAL;}
  if(scenario==4){a.gmu.hfi.dcvs_table.cx_votes[1].freq=300000;want=-EINVAL;}
  if(scenario==5){set_error=-EBUSY;want=-EBUSY;}
  if(scenario==6){rounded=true;want=-ERANGE;}
  if(scenario==7){bulk_error=-EIO;want=-EIO;}
  if(scenario==8)a.gmu.hub_clk=&clocks[5];
  assert(a6xx_gmu_enable_clks(&a)==want);
  assert(a.dev.state==(want?2:KGSL_STATE_AWARE) && rdpm==!want && a.gmu.clks_on==!want);
  if(scenario<=4 && scenario>0)assert(!sets && !enables);
  if(!want)assert(enables==1 && clocks[1].rate==200000000);
  if(scenario==8)assert(clocks[5].rate==150000000 && sets==2);
  ++cases;
 }
 for(int scenario=0;scenario<6;++scenario) {struct adreno_device a=reset();if(scenario==1)opp_count=3;assert(!setup_cx_arc_votes(&a.gmu,&cx,&mx));struct hfi_dcvstable_cmd *t=&a.gmu.hfi.dcvs_table;t->gpu_level_num=1;t->gx_votes[0].freq=0;t->gx_votes[0].vote=123;
  if(scenario==2)t->gmu_level_num=0;
  if(scenario==3)t->gmu_level_num=5;
  if(scenario==4)t->gpu_level_num=17;
  if(scenario==5)send_error=-ETIMEDOUT;
  assert(a6xx_hfi_send_dcvstbl_v1(&a)==(scenario>=2 && scenario<=4?-EINVAL:send_error));
  if(scenario>=2 && scenario<=4)assert(!sends);
  else {assert(sends==1 && sent.gmu_level_num==t->gmu_level_num && sent.gpu_level_num==1);assert(sizeof(sent)==172 && sent.hdr==0x2b04);for(unsigned i=0;i<t->gmu_level_num;++i)assert(!memcmp(&sent.cx_votes[i],&t->cx_votes[i],sizeof(struct opp_desc)));assert(sent.gx_votes[0].vote==123);}
  ++cases;
 }
 printf("%u GMU clock/OPP/RPMh/HFI scenarios passed (real ARC transform and captured metadata; clock/OPP/firmware calls mocked)\n",cases);return 0;
}
