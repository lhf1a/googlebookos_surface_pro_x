/* One model instance crosses the real loader, RPMh/HFI, governor and setter. */
#include "joint-prefix.h"
#include "joint-bus.h"
static int dcvs_error,dcvs_calls,last_dcvs_level,trace_calls;
static struct db_entry *lookup(const char *name);
static u32 golden_word(unsigned row,unsigned bcm_index);
static int a6xx_gmu_dcvs_set(struct adreno_device *a,u32 freq,int level)
{
 check(a==&adreno&&freq==UINT32_MAX,"DCVS instance/frequency");
 dcvs_calls++;last_dcvs_level=level;
 struct hfi_bwtable_cmd *h=&a->gmu.hfi.bw_table;
 check(level>=0&&(u32)level<h->bw_level_num,"DCVS index outside constructed HFI table");
 if(level>=0&&(u32)level<h->bw_level_num)
  for(unsigned j=0;j<3;j++)check(h->ddr_cmd_data[level][j]==golden_word(level,j),"live DCVS index consumed wrong loader-derived HFI row");
 return dcvs_error;
}
static void trace_kgsl_buslevel(struct kgsl_device *d,unsigned active,int level)
{check(d==&gpu&&active==gpu.pwrctrl.active_pwrlevel&&level==gpu.pwrctrl.cur_buslevel,"setter trace ownership");trace_calls++;}
static struct kgsl_device *dev_get_drvdata(struct device *d){return d->data;}
static void mutex_lock(int *m){check(!*m,"recursive modeled device mutex");*m=1;}
static void mutex_unlock(int *m){check(*m,"unheld modeled device mutex");*m=0;}
#define INVALID_DCVS_IDX UINT32_MAX
#include "joint-hints.h"
#include "joint-consumers.h"
static int set_bus(struct kgsl_device *d,int level,u32 ab)
{check(d==&gpu,"function-table device");return a6xx_gmu_bus_set(&adreno,level,ab);}
static const struct kgsl_functable ftbl={set_bus};
#include "joint-reset.h"

struct db_entry {const char *name;u32 address;size_t len;u8 bytes[64];};
#include "joint-target.h"
static struct db_entry entries[ARRAY_SIZE(target_entries)];
static const char *aux_error_name;
static int aux_error;
static struct db_entry *lookup(const char *name)
{for(unsigned i=0;i<ARRAY_SIZE(entries);i++)if(!strcmp(entries[i].name,name))return &entries[i];check(false,"unknown target cmd-db resource");return NULL;}
static const void *cmd_db_read_aux_data(const char *name,size_t *len)
{if(aux_error&&aux_error_name&&!strcmp(aux_error_name,name))return ERR_PTR(aux_error);struct db_entry *e=lookup(name);*len=e->len;return e->bytes;}
static u32 cmd_db_read_addr(const char *name){return lookup(name)->address;}
static int cmd_db_read_slave_id(const char *name){(void)name;return 5;}
#define CMD_DB_HW_BCM 5
#define kzalloc(size,flags) kcalloc(1,(size),(flags))
#include "joint-rpmh.h"

/* Independent scalar oracle: raw little-endian metadata and fixed SC8180X
 * resource bus widths, not calls into tcs_cmd_data/BCM_TCS_CMD under test. */
static u32 raw_le(const u8 *b,unsigned bytes)
{u32 value=0;for(unsigned i=0;i<bytes;i++)value|=(u32)b[i]<<(8*i);return value;}
static u32 golden_word(unsigned row,unsigned j)
{
 static const char *const names[]={"MC0","SH0","ACV"};
 static const unsigned buswidth[]={4,16,0};
 struct db_entry *e=lookup(names[j]);
 bool commit=j==2||e->bytes[6]!=lookup(names[j+1])->bytes[6];
 u64 y=0;u32 ib=source_ddr[row];
 if(j==2)y=ib?8:0;
 else {u32 unit=raw_le(e->bytes,4),width=raw_le(e->bytes+4,2);check(unit&&width,"golden target metadata");y=((u64)ib*width/buswidth[j])*1000/unit;if(ib&&!y)y=1;if(y>0x3fff)y=0x3fff;}
 return (commit?0x40000000U:0)|(y?0x20000000U:0)|(u32)y;
}
static void verify_hfi(void)
{
 struct hfi_bwtable_cmd *h=&adreno.gmu.hfi.bw_table;
 check(gpu.pwrctrl.ddr_table_count==13&&!memcmp(gpu.pwrctrl.ddr_table,source_ddr,sizeof(source_ddr)),"loader lost complete13-entry policy");
 check(h->bw_level_num==13&&h->ddr_cmds_num==3,"HFI coalesced/truncated DDR policy");
 check(h->cnoc_cmds_num==4,"HFI omitted target CNOC route");
 const char *names[]={"MC0","SH0","ACV"};u32 waits=0;
 for(unsigned j=0;j<3;j++){check(h->ddr_cmd_addrs[j]==lookup(names[j])->address,"HFI target resource address mismatch");if(j==2||lookup(names[j])->bytes[6]!=lookup(names[j+1])->bytes[6])waits|=1U<<j;}
 check(h->ddr_wait_bitmask==waits,"HFI target VCD wait mask");
 for(unsigned i=0;i<13;i++)for(unsigned j=0;j<3;j++)check(h->ddr_cmd_data[i][j]==golden_word(i,j),"HFI word does not convert exact full-table DDR input with target metadata");
 const char *cnoc[]={"SH2","SN15","SN3","CN0"};waits=0;
 for(unsigned j=0;j<4;j++){
  struct db_entry *e=lookup(cnoc[j]);bool commit=j==3||e->bytes[6]!=lookup(cnoc[j+1])->bytes[6];if(commit)waits|=1U<<j;
  check(h->cnoc_cmd_addrs[j]==e->address,"CNOC target address mismatch");
  for(unsigned i=0;i<2;i++){u64 y=((u64)source_cnoc[i]*raw_le(e->bytes+4,2)/8)*1000/raw_le(e->bytes,4);if(source_cnoc[i]&&!y)y=1;if(y>0x3fff)y=0x3fff;u32 word=(commit?0x40000000U:0)|(y?0x20000000U:0)|(u32)y;check(h->cnoc_cmd_data[i][j]==word,"CNOC target metadata conversion mismatch");}
 }
 check(h->cnoc_wait_bitmask==waits,"CNOC target VCD wait mask");
 check(h->hdr==CREATE_MSG_HDR(H2F_MSG_BW_VOTE_TBL,sizeof(*h),HFI_MSG_CMD),"HFI header size/type");
 for(unsigned i=13;i<MAX_GX_LEVELS;i++)for(unsigned j=0;j<MAX_BW_CMDS;j++)check(h->ddr_cmd_data[i][j]==0,"HFI unused rows were not cleared");
}
static void consumer_failure_retry(struct device *busmon,bool fail_dcvs)
{
 struct kgsl_pwrctrl *p=&gpu.pwrctrl;p->active_pwrlevel=2;p->bus_mod=0;p->cur_buslevel=0;p->bus_control=true;p->power_flags=1UL<<KGSL_PWRFLAGS_AXI_ON;gpu.pwrscale.enabled=true;
 gpu.pwrscale.bus_profile.flag=DEVFREQ_FLAG_FAST_HINT;gpu.pwrscale.bus_profile.percent_ab=30;unsigned long freq=p->pwrlevels[2].gpu_freq;
 if(fail_dcvs)dcvs_error=-EIO;else icc_vote_error=-EIO;
 check(kgsl_busmon_target(busmon,&freq,0)==-EIO,"joint consumer error was hidden");
 check(p->bus_mod==0&&p->bus_percent_ab==0&&p->cur_ab==0,"failed hint lost original request/AB state");
 check(p->cur_buslevel==(fail_dcvs?0:11),"partial DCVS/ICC result was misattributed");
 if(!fail_dcvs)check(p->icc_dirty,"failed ICC rollback did not require retry");
 int sent=dcvs_calls;dcvs_error=icc_vote_error=0;gpu.pwrscale.bus_profile.flag=DEVFREQ_FLAG_FAST_HINT;
 check(!kgsl_busmon_target(busmon,&freq,0)&&p->cur_buslevel==11&&p->bus_mod==1&&!p->icc_dirty,"same constructed HFI policy did not support consumer retry");
 check(dcvs_calls==sent+(fail_dcvs?1:0),"ICC-only retry resent already accepted GMU index");
}
static void consumer_chain(struct device *busmon)
{
 struct kgsl_pwrctrl *p=&gpu.pwrctrl;
 /* Do not reinstall a fixture table: every consumer below uses the exact
  * same allocation published by kgsl_bus_init and read by build_bw_table. */
 for(unsigned i=0;i<7;i++){
  check(p->pwrlevels[i].gpu_freq==source_rows[i].freq&&p->pwrlevels[i].bus_freq==source_rows[i].def&&p->pwrlevels[i].bus_min==source_rows[i].min&&p->pwrlevels[i].bus_max==source_rows[i].max,"loader frequency/default/min/max did not reach consumers intact");
  p->default_pwrlevel=i;u32 minimum=source_ddr[source_rows[i].min];
  check(!kgsl_bus_vote_bootstrap(&gpu,true)&&last_peak==minimum,"bootstrap used default/max or global minimum instead of this-frequency minimum");
  icc_vote_error=-EIO;check(kgsl_bus_vote_bootstrap(&gpu,false)==-EIO&&p->bootstrap_ib==minimum&&p->icc_dirty,"bootstrap floor lost on ICC failure");
  icc_vote_error=0;check(!kgsl_bus_vote_bootstrap(&gpu,false)&&!p->bootstrap_ib,"bootstrap retry/release");
 }
 p->bus_control=true;p->power_flags=1UL<<KGSL_PWRFLAGS_AXI_ON;gpu.pwrscale.enabled=true;
 for(unsigned i=0;i<7;i++){
  p->active_pwrlevel=i;p->bus_mod=0;p->cur_buslevel=-1;
  unsigned long freq=p->pwrlevels[i].gpu_freq;
  int expected=source_rows[i].def;
  for(unsigned direction=0;direction<2;direction++)for(unsigned step=0;step<20;step++){
   gpu.pwrscale.bus_profile.flag=direction?DEVFREQ_FLAG_SLOW_HINT:DEVFREQ_FLAG_FAST_HINT;gpu.pwrscale.bus_profile.percent_ab=30;
   expected+=direction?-1:1;if(expected<(int)source_rows[i].min)expected=source_rows[i].min;if(expected>(int)source_rows[i].max)expected=source_rows[i].max;
   check(!kgsl_busmon_target(busmon,&freq,0),"hint consumer returned error");
   check((int)p->pwrlevels[i].bus_freq+p->bus_mod==expected&&p->cur_buslevel==expected&&last_dcvs_level==expected,"hint bypassed per-frequency min/max or setter selected wrong HFI index");
   check(last_ab==((u64)30*source_ddr[12]/100000)*1000,"runtime AB/ICC conversion changed");
   check(!gpu.mutex&&!gpu.pwrscale.bus_profile.flag,"busmon modeled lock/flag ownership");
  }
 }
}
int main(int argc,char **argv)
{
 if(argc!=2)return 2;
 reset();memcpy(entries,target_entries,sizeof(entries));aux_error=0;aux_error_name=NULL;adreno.a680=true;
 struct platform_device pdev={.dev={.of_node=&root,.data=&gpu}};gpu.pdev=&pdev;struct device busmon={.data=&gpu};
 struct hfi_bwtable_cmd *h=&adreno.gmu.hfi.bw_table;memset(h,0xa5,sizeof(*h));struct hfi_bwtable_cmd saved=*h;
 if(!strcmp(argv[1],"ten-enabled")){
  gpu.pwrctrl.num_pwrlevels=opp_count=10;for(unsigned i=0;i<10;i++){gpu.pwrctrl.pwrlevels[i].gpu_freq=target_ten_hz[i];opps[i].freq=target_ten_hz[i];}
  before=gpu.pwrctrl;int rc=kgsl_bus_init(&gpu,&pdev);check(rc==-EINVAL,"10enabled+7policy was not explicitly rejected");check(!memcmp(&gpu.pwrctrl,&before,sizeof(before))&&!memcmp(h,&saved,sizeof(saved)),"incomplete policy published consumer state");if(!rc)kgsl_bus_close(&gpu);check(allocations==frees,"incomplete policy leaked temporary table");printf("ten-enabled: ret=%d failures=%d hfi-built=0\n",rc,failures);return failures?1:0;
 }
 before=gpu.pwrctrl;int rc=kgsl_bus_init(&gpu,&pdev);
 if(rc){check(false,"joint chain stopped at indexed loader");printf("%s: loader=%d failures=%d hfi-built=0\n",argv[1],rc,failures);return 1;}
 int fail_n=0;if(sscanf(argv[1],"alloc-%d",&fail_n)==1)allocation_error_at=allocations+fail_n;
 const char *names[]={"MC0","SH0","ACV","SH2","SN15","SN3","CN0"};
 for(unsigned i=0;i<ARRAY_SIZE(names);i++){char name[32];snprintf(name,sizeof(name),"metadata-%s",names[i]);if(!strcmp(argv[1],name)){aux_error=-EIO;aux_error_name=names[i];}}
 int expected=fail_n?-ENOMEM:aux_error?-EIO:0;int owned=allocations-frees;u32 *loaded_table=gpu.pwrctrl.ddr_table;
 rc=build_bw_table(&adreno);
 check(rc==expected,"loader-fed RPMh build returned wrong error");check(allocations-frees==owned,"RPMh error/success leaked table temporaries or freed loader owner");
 if(expected){check(!memcmp(h,&saved,sizeof(saved)),"failed RPMh build partially published HFI command");allocation_error_at=0;aux_error=0;rc=build_bw_table(&adreno);check(!rc,"same loader table could not retry RPMh build");}
 if(!rc){check(gpu.pwrctrl.ddr_table==loaded_table,"RPMh replaced loader table allocation");verify_hfi();if(!strcmp(argv[1],"dcvs-error"))consumer_failure_retry(&busmon,true);if(!strcmp(argv[1],"icc-error"))consumer_failure_retry(&busmon,false);consumer_chain(&busmon);check(gpu.pwrctrl.ddr_table==loaded_table,"consumer chain replaced loader table allocation");}
 kgsl_bus_close(&gpu);check(allocations==frees,"joint chain leaked loader/provider table");
 printf("%s: failures=%d ddr=13 gpu=7 hfi_ddr_words=39 hfi_cnoc_words=8 hint_steps=280 dcvs=%d alloc=%d free=%d\n",argv[1],failures,dcvs_calls,allocations,frees);
 return failures?1:0;
}
