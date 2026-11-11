/* Real kgsl_bus.c executes; OF/OPP/ICC and allocation leaves are fault models. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#define dev_err(dev, ...) ((void)(dev), fprintf(stderr, __VA_ARGS__))
typedef uint32_t u32;
typedef uint64_t u64;
typedef int64_t s64;
#define U32_MAX UINT32_MAX
#define GFP_KERNEL 0
#define KGSL_MAX_PWRLEVELS 16
#define MAX_GX_LEVELS 16
#define KGSL_PWRFLAGS_AXI_ON 2
#define EPROBE_DEFER 517
#define MBps_to_icc(v) ((v)*1000U)
#define kBps_to_icc(v) (v)
#define max(a,b) ((a)>(b)?(a):(b))
#define ERR_PTR(n) ((void *)(intptr_t)(n))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p)>=(uintptr_t)-4095)
#define IS_ERR_OR_NULL(p) (!(p)||IS_ERR(p))
#define clamp_t(t,x,lo,hi) ({t _x=(x),_l=(lo),_h=(hi);_x<_l?_l:_x>_h?_h:_x;})
enum kgsl_bus_vote {KGSL_BUS_VOTE_OFF,KGSL_BUS_VOTE_ON,KGSL_BUS_VOTE_MINIMUM};
struct kgsl_pwrlevel {unsigned int gpu_freq,bus_freq,bus_min,bus_max,acd_level;u32 voltage_level;};
struct icc_path {int id;};
struct kgsl_pwrctrl {
 struct kgsl_pwrlevel pwrlevels[KGSL_MAX_PWRLEVELS];
 unsigned int num_pwrlevels,active_pwrlevel,default_pwrlevel;
 int ddr_table_count,bus_mod;u32 *ddr_table;struct icc_path *icc_path;
 u32 cur_ab,cur_ib,bootstrap_ib,bus_percent_ab,bus_width;unsigned long power_flags,bus_ab_mbytes;bool icc_dirty;
};
struct device_node {int kind,index,refs;bool available;};
struct device {struct device_node *of_node;};
struct platform_device {struct device dev;};
struct kgsl_device;
struct kgsl_functable {int (*gpu_bus_set)(struct kgsl_device *,int,u32);};
struct kgsl_device {struct kgsl_pwrctrl pwrctrl;const struct kgsl_functable *ftbl;};
struct dev_pm_opp {unsigned long freq,peak,avg;};
struct fixture_row {u32 freq,def,min,max;};
#include "source-policy.h"
static int failures;
static void check(bool ok,const char *why) {if(!ok){fprintf(stderr,"FAIL: %s\n",why);failures++;}}
static struct kgsl_device gpu;
static struct kgsl_pwrctrl before;
static struct device_node root={.kind=0},parent={.kind=1},nodes[20];
static struct fixture_row rows[20];
static u32 table[32];
static int row_count,table_len,opp_count,opp_refs,opp_gets,opp_puts,opp_error_at,opp_count_error;
static struct dev_pm_opp opps[16];
static bool opp_disabled[16];
static bool policy_present,table_present,parent_present,native_mode;
static const char *policy_mode;
static int mode_count,mode_read_error,icc_names,icc_index,table_count_error,table_read_error;
static int row_bad_index,row_bad_field,row_bad_count,row_read_error_field;
static int allocations,frees,allocation_error_at,icc_gets,icc_puts,icc_vote_error;
static u32 last_peak,last_ab;
static struct icc_path actual_path;
static struct icc_path *path_result;
static const char *field_names[]={"qcom,gpu-freq","qcom,bus-freq","qcom,bus-min","qcom,bus-max"};
static int field(const char *name){for(int i=0;i<4;i++)if(!strcmp(name,field_names[i]))return i;return -1;}
static bool of_device_is_compatible(struct device_node *n,const char *name){(void)n;check(!strcmp(name,"qcom,adreno-680.1"),"native compatible");return native_mode;}
static void *of_find_property(struct device_node *n,const char *name,int *len){(void)n;(void)len;if(!strcmp(name,"qcom,kgsl-bus-policy"))return policy_present?&root:NULL;if(!strcmp(name,"qcom,bus-table-ddr"))return table_present?&root:NULL;return NULL;}
static int of_property_count_strings(struct device_node *n,const char *name){(void)n;if(!strcmp(name,"qcom,kgsl-bus-policy"))return mode_count;return icc_names;}
static int of_property_match_string(struct device_node *n,const char *name,const char *value){(void)n;(void)name;(void)value;return icc_index;}
static int of_property_read_string(struct device_node *n,const char *name,const char **out){(void)n;(void)name;if(mode_read_error)return mode_read_error;*out=policy_mode;return 0;}
static int of_property_count_elems_of_size(struct device_node *n,const char *name,int size){check(size==4,"u32 cell width");if(n->kind==0){check(!strcmp(name,"qcom,bus-table-ddr"),"table property");return table_count_error?table_count_error:table_len;}int f=field(name);check(f>=0,"known row field");if(n->index==row_bad_index&&f==row_bad_field)return row_bad_count;return 1;}
static int of_property_read_u32_array(struct device_node *n,const char *name,u32 *values,int count){(void)n;(void)name;if(table_read_error)return table_read_error;check(count==table_len,"table read length");memcpy(values,table,count*sizeof(*values));return 0;}
static int of_property_read_u32(struct device_node *n,const char *name,u32 *value){int f=field(name);if(n->index==row_bad_index&&f==row_read_error_field)return -EIO;struct fixture_row *r=&rows[n->index];u32 fields[]={r->freq,r->def,r->min,r->max};*value=fields[f];return 0;}
static struct device_node *of_get_child_by_name(struct device_node *n,const char *name){(void)n;check(!strcmp(name,"qcom,gpu-pwrlevels"),"indexed native container");if(!parent_present)return NULL;parent.refs++;return &parent;}
static bool of_device_is_available(struct device_node *n){return n&&n->available;}
static void of_node_put(struct device_node *n){if(n){check(n->refs>0,"OF ref underflow");n->refs--;}}
static struct device_node *of_get_next_available_child(struct device_node *p,struct device_node *prev){check(p==&parent,"OF parent");int start=prev?prev->index+1:0;of_node_put(prev);for(int i=start;i<row_count;i++)if(nodes[i].available){nodes[i].refs++;return &nodes[i];}return NULL;}
#define for_each_available_child_of_node(p,c) for((c)=of_get_next_available_child((p),NULL);(c);(c)=of_get_next_available_child((p),(c)))
static int dev_pm_opp_get_opp_count(struct device *d){(void)d;if(opp_count_error)return opp_count_error;int count=0;for(int i=0;i<opp_count;i++)count+=!opp_disabled[i];return count;}
static struct dev_pm_opp *dev_pm_opp_find_freq_exact(struct device *d,unsigned long freq,bool available){(void)d;check(available,"enabled OPP lookup");opp_gets++;if(opp_gets==opp_error_at)return ERR_PTR(-EIO);for(int i=0;i<opp_count;i++)if(opps[i].freq==freq&&!opp_disabled[i]){opp_refs++;return &opps[i];}return ERR_PTR(-ERANGE);}
static unsigned long dev_pm_opp_get_bw(struct dev_pm_opp *opp,bool peak,int idx){check(opp_refs==1&&idx==0,"OPP reference/index");return peak?opp->peak:opp->avg;}
static void dev_pm_opp_put(struct dev_pm_opp *opp){(void)opp;check(opp_refs==1,"OPP put owner");opp_refs--;opp_puts++;}
static void *kcalloc(size_t n,size_t s,int flags){(void)flags;allocations++;if(allocations==allocation_error_at){allocations--;return NULL;}return calloc(n,s);}
static void kfree(void *p){check(!IS_ERR(p),"free error pointer");if(p){frees++;free(p);}}
static int kgsl_get_ddrtype(void){return -ENOENT;}
static struct icc_path *of_icc_get(struct device *d,const char *name){(void)d;(void)name;icc_gets++;check(!memcmp(&gpu.pwrctrl,&before,sizeof(before)),"partial publication before ICC acquisition");return path_result;}
static void icc_put(struct icc_path *p){check(!IS_ERR(p),"ICC put error");if(p)icc_puts++;}
static int icc_set_bw(struct icc_path *p,u32 ab,u32 ib){check(p==&actual_path,"ICC path owner");last_ab=ab;last_peak=ib;return icc_vote_error;}
static bool test_bit(unsigned int bit,unsigned long *p){return !!(*p&(1UL<<bit));}
static u64 div_u64(u64 x,u32 y){return x/y;}
/* REAL_BUS_SOURCE */
static int set_bus(struct kgsl_device *d,int level,u32 ab){return kgsl_bus_set_icc(d,ab,d->pwrctrl.ddr_table[level]);}
static const struct kgsl_functable ftbl={set_bus};
static void reset(void){
 memset(&gpu,0,sizeof(gpu));memset(opps,0,sizeof(opps));memset(opp_disabled,0,sizeof(opp_disabled));memset(rows,0,sizeof(rows));memset(nodes,0,sizeof(nodes));
 memcpy(table,source_ddr,sizeof(source_ddr));table_len=sizeof(source_ddr)/sizeof(*source_ddr);
 row_count=sizeof(source_rows)/sizeof(*source_rows);memcpy(rows,source_rows,sizeof(source_rows));
 for(int i=0;i<20;i++){nodes[i]=(struct device_node){.kind=2,.index=i,.available=true};}
 gpu.ftbl=&ftbl;gpu.pwrctrl.num_pwrlevels=opp_count=7;gpu.pwrctrl.default_pwrlevel=gpu.pwrctrl.active_pwrlevel=6;
 for(int i=0;i<7;i++){gpu.pwrctrl.pwrlevels[i]=(struct kgsl_pwrlevel){rows[i].freq,99,98,100,77,(u32)(100+i)};opps[i].freq=rows[i].freq;}
 gpu.pwrctrl.cur_ab=123;gpu.pwrctrl.cur_ib=456;gpu.pwrctrl.bootstrap_ib=789;gpu.pwrctrl.icc_dirty=true;
 policy_present=table_present=parent_present=native_mode=true;policy_mode="indexed";mode_count=icc_names=1;mode_read_error=icc_index=table_count_error=table_read_error=0;
 row_bad_index=row_bad_field=row_read_error_field=-1;row_bad_count=1;opp_count_error=opp_refs=opp_gets=opp_puts=opp_error_at=0;root.refs=parent.refs=0;parent.available=true;
 allocations=frees=allocation_error_at=icc_gets=icc_puts=icc_vote_error=0;path_result=&actual_path;last_peak=last_ab=0;
}
static int scenario(const char *s,bool *peak,bool *owned){
 struct kgsl_pwrctrl *p=&gpu.pwrctrl;int expected=0;*peak=false;*owned=false;
 if(!strncmp(s,"peak-",5)){
  *peak=true;policy_present=false;p->num_pwrlevels=opp_count=4;p->active_pwrlevel=p->default_pwrlevel=1;
  for(int i=0;i<4;i++){opps[i]=(struct dev_pm_opp){100000000UL*(i+1),100000UL*(i/2+1),0};p->pwrlevels[3-i].gpu_freq=opps[i].freq;}
  if(!strcmp(s,"peak-ok"))return 0;
  if(!strcmp(s,"peak-no-first")){opps[0].peak=0;return -ENODATA;}
  if(!strcmp(s,"peak-no-last")){opps[3].peak=0;return -ENODATA;}
  if(!strcmp(s,"peak-decreasing")){opps[1].peak=1;return -EINVAL;}
  if(!strcmp(s,"peak-overflow")){opps[3].peak=1UL+U32_MAX;return -ERANGE;}
  if(!strcmp(s,"peak-avg")){opps[1].avg=1;return -EOPNOTSUPP;}
  if(!strcmp(s,"peak-opp-error")){opp_error_at=3;return -EIO;}
  if(!strcmp(s,"peak-missing-opp")){opp_count=2;return -ERANGE;}
  if(!strcmp(s,"peak-icc-names")){icc_names=2;return -EINVAL;}
  if(!strcmp(s,"peak-icc-index")){icc_index=1;return -EINVAL;}
  if(!strcmp(s,"peak-duplicate-freq")){p->pwrlevels[2].gpu_freq=p->pwrlevels[3].gpu_freq;return -EINVAL;}
  if(!strcmp(s,"peak-alloc")){allocation_error_at=1;return -ENOMEM;}
  if(!strcmp(s,"peak-icc-defer")){path_result=ERR_PTR(-EPROBE_DEFER);return -EPROBE_DEFER;}
  if(!strcmp(s,"peak-icc-null")){path_result=NULL;return -ENODEV;}
  if(!strcmp(s,"peak-zero-count")){p->num_pwrlevels=0;return -EINVAL;}
  if(!strcmp(s,"peak-count-big")){p->num_pwrlevels=17;return -EINVAL;}
  if(!strcmp(s,"peak-active-big")){p->active_pwrlevel=4;return -EINVAL;}
  if(!strcmp(s,"peak-default-big")){p->default_pwrlevel=4;return -EINVAL;}
  if(!strcmp(s,"peak-hfi-full")){p->num_pwrlevels=opp_count=16;for(int i=0;i<16;i++){opps[i]=(struct dev_pm_opp){10000000UL*(i+1),10000UL*(i+1),0};p->pwrlevels[15-i].gpu_freq=opps[i].freq;}return -E2BIG;}
  return INT_MIN;
 }
 if(!strcmp(s,"reference"))return 0;
 if(!strcmp(s,"non-native-policy")){native_mode=false;return -EOPNOTSUPP;}
 if(!strcmp(s,"legacy-ok")){native_mode=policy_present=false;table_len=4;table[0]=0;table[1]=100000;table[2]=200000;table[3]=400000;p->num_pwrlevels=2;p->active_pwrlevel=p->default_pwrlevel=1;p->pwrlevels[0]=(struct kgsl_pwrlevel){700000000,3,1,3,77,100};p->pwrlevels[1]=(struct kgsl_pwrlevel){177000000,1,1,2,77,101};return 0;}
 if(!strcmp(s,"row-reordered")){struct fixture_row temp=rows[1];rows[1]=rows[5];rows[5]=temp;return 0;}
 if(!strcmp(s,"off-first")){struct fixture_row temp=rows[0];rows[0]=rows[7];rows[7]=temp;return 0;}
 if(!strcmp(s,"indexed-ignores-peak")){for(int i=0;i<7;i++)opps[i].peak=7000-i;return 0;}
 if(!strcmp(s,"table-minimum")){table_len=2;table[1]=100;for(int i=0;i<7;i++)rows[i].def=rows[i].min=rows[i].max=1;return 0;}
 if(!strcmp(s,"table-hfi-maximum")){table_len=16;table[13]=8000000;table[14]=9000000;table[15]=10000000;rows[0].max=15;return 0;}
 if(!strcmp(s,"table-u32-maximum")){table[12]=U32_MAX;return 0;}
 if(!strcmp(s,"unknown")){policy_mode="peak";return -EINVAL;}
 if(!strcmp(s,"empty-mode")){policy_mode="";return -EINVAL;}
 if(!strcmp(s,"multi-mode")){mode_count=2;return -EINVAL;}
 if(!strcmp(s,"empty-property")){mode_count=0;return -EINVAL;}
 if(!strcmp(s,"unterminated-mode")){mode_count=-EILSEQ;return -EILSEQ;}
 if(!strcmp(s,"read-mode-error")){mode_read_error=-EIO;return -EIO;}
 if(!strcmp(s,"no-table")){table_present=false;return -ENOENT;}
 if(!strcmp(s,"short-table")){table_len=1;return -EINVAL;}
 if(!strcmp(s,"empty-table")){table_len=0;return -EINVAL;}
 if(!strcmp(s,"huge-table")){table_len=17;return -E2BIG;}
 if(!strcmp(s,"malformed-table")){table_count_error=-EINVAL;return -EINVAL;}
 if(!strcmp(s,"read-table-error")){table_read_error=-EIO;return -EIO;}
 if(!strcmp(s,"table-off")){table[0]=1;return -EINVAL;}
 if(!strcmp(s,"table-equal")){table[8]=table[7];return -EINVAL;}
 if(!strcmp(s,"table-decrease")){table[12]=1;return -EINVAL;}
 if(!strcmp(s,"no-parent")){parent_present=false;return -ENOENT;}
 if(!strcmp(s,"disabled-parent")){parent.available=false;return -ENOENT;}
 if(!strcmp(s,"duplicate-row")){rows[1]=rows[0];return -EINVAL;}
 if(!strcmp(s,"extra-duplicate")){rows[8]=rows[0];row_count=9;return -EINVAL;}
 if(!strcmp(s,"fuzzy-frequency")){rows[0].freq--;return -EINVAL;}
 if(!strcmp(s,"missing-row")){p->pwrlevels[6].bus_freq=4;p->pwrlevels[6].bus_min=3;p->pwrlevels[6].bus_max=5;nodes[6].available=false;return -EINVAL;}
 if(!strcmp(s,"extra-active")){rows[8]=(struct fixture_row){999000000,4,3,5};row_count=9;return -EINVAL;}
 if(!strcmp(s,"extra-disabled")){rows[8]=(struct fixture_row){999000000,4,3,5};nodes[8].available=false;row_count=9;return 0;}
 if(!strcmp(s,"duplicate-off")){rows[8]=rows[7];row_count=9;return -EINVAL;}
 if(!strcmp(s,"missing-off")){row_count=7;return -EINVAL;}
 if(!strcmp(s,"off-triple")){rows[7].min=1;return -EINVAL;}
 if(!strcmp(s,"min-zero")){rows[4].min=0;return -EINVAL;}
 if(!strcmp(s,"default-zero")){rows[4].def=0;return -EINVAL;}
 if(!strcmp(s,"min-gt-default")){rows[4].min=rows[4].def+1;return -EINVAL;}
 if(!strcmp(s,"default-gt-max")){rows[4].def=rows[4].max+1;return -EINVAL;}
 if(!strcmp(s,"index-equal-count")){rows[4].max=13;return -EINVAL;}
 if(!strcmp(s,"index-overflow")){rows[4].max=U32_MAX;return -EINVAL;}
 if(!strcmp(s,"global-max")){rows[0].max=11;rows[0].def=11;rows[0].min=11;return -EINVAL;}
 if(!strcmp(s,"opp-count-extra")){opps[7].freq=700000000;opp_count=8;return -EINVAL;}
 if(!strcmp(s,"opp-count-missing")){opp_count=6;return -EINVAL;}
 if(!strcmp(s,"disabled-opp")){opp_disabled[4]=true;return -EINVAL;}
 if(!strcmp(s,"opp-count-error")){opp_count_error=-EIO;return -EIO;}
 if(!strcmp(s,"opp-lookup-first")){opp_error_at=1;return -EIO;}
 if(!strcmp(s,"opp-lookup-last")){opp_error_at=7;return -EIO;}
 if(!strcmp(s,"opp-exact-mismatch")){opps[4].freq++;return -ERANGE;}
 if(!strcmp(s,"opp-avg")){opps[6].avg=1;return -EOPNOTSUPP;}
 if(!strcmp(s,"opp-duplicate")){p->pwrlevels[4].gpu_freq=p->pwrlevels[3].gpu_freq;return -EINVAL;}
 if(!strcmp(s,"opp-zero-freq")){p->pwrlevels[4].gpu_freq=0;return -EINVAL;}
 if(!strcmp(s,"count-zero")){p->num_pwrlevels=0;return -EINVAL;}
 if(!strcmp(s,"count-too-big")){p->num_pwrlevels=17;return -EINVAL;}
 if(!strcmp(s,"count-hfi")){p->num_pwrlevels=opp_count=16;return -E2BIG;}
 if(!strcmp(s,"active-invalid")){p->active_pwrlevel=7;return -EINVAL;}
 if(!strcmp(s,"default-invalid")){p->default_pwrlevel=7;return -EINVAL;}
 if(!strcmp(s,"icc-names")){icc_names=2;return -EINVAL;}
 if(!strcmp(s,"icc-index")){icc_index=1;return -EINVAL;}
 if(!strcmp(s,"alloc")){allocation_error_at=1;return -ENOMEM;}
 if(!strcmp(s,"icc-null")){path_result=NULL;return -ENODEV;}
 if(!strcmp(s,"icc-defer")){path_result=ERR_PTR(-EPROBE_DEFER);return -EPROBE_DEFER;}
 if(!strcmp(s,"icc-error")){path_result=ERR_PTR(-EIO);return -EIO;}
 if(!strcmp(s,"already-owned")){p->ddr_table=table;p->ddr_table_count=13;p->icc_path=&actual_path;*owned=true;return -EBUSY;}
 if(!strcmp(s,"ten-target-frequencies")){
  unsigned hz[]={700000000,661000000,590000000,514000000,500000000,461000000,405000000,315000000,256000000,177000000};
  p->num_pwrlevels=opp_count=10;for(int i=0;i<10;i++){p->pwrlevels[i].gpu_freq=hz[i];opps[i].freq=hz[i];}return -EINVAL;
 }
 if(!strncmp(s,"field-",6)){
  int f,kind;if(sscanf(s,"field-%d-%d",&f,&kind)!=2||f<0||f>3||kind<0||kind>2)return INT_MIN;
  row_bad_index=4;row_bad_field=f;if(kind==0){row_bad_count=-EINVAL;return -EINVAL;}if(kind==1){row_bad_count=2;return -EINVAL;}row_bad_field=-1;row_read_error_field=f;return -EIO;
 }
 (void)expected;return INT_MIN;
}
int main(int argc,char **argv){
 if(argc!=2)return 2;
 reset();bool peak,owned;int expected=scenario(argv[1],&peak,&owned);if(expected==INT_MIN)return 2;
 before=gpu.pwrctrl;struct platform_device pdev={.dev.of_node=&root};int ret=kgsl_bus_init(&gpu,&pdev);
 check(ret==expected,"init returned wrong errno");check(!opp_refs,"OPP reference leak");check(parent.refs==0,"parent OF ref leak");for(int i=0;i<20;i++)check(nodes[i].refs==0,"row OF ref leak");
 if(expected){check(!memcmp(&gpu.pwrctrl,&before,sizeof(before)),"failed transaction changed published state");check(allocations==frees,"failed allocation/table ownership");}
 else if(!ret){
  struct kgsl_pwrctrl *p=&gpu.pwrctrl;check(p->icc_path==&actual_path&&p->ddr_table,"successful table/path publication");
  if(!failures){struct kgsl_pwrctrl published=*p;int old_allocs=allocations,old_gets=icc_gets;check(kgsl_bus_init(&gpu,&pdev)==-EBUSY&&!memcmp(p,&published,sizeof(published))&&allocations==old_allocs&&icc_gets==old_gets,"repeat init changed existing owner");}
  if(!native_mode){check(p->ddr_table_count==4&&!memcmp(p->pwrlevels,before.pwrlevels,sizeof(p->pwrlevels)),"legacy regression");}
  else if(peak){check(p->ddr_table_count==3&&p->ddr_table[1]==100000&&p->ddr_table[2]==200000,"peak-only table regression");for(int i=0;i<4;i++)check(p->pwrlevels[i].bus_freq==(unsigned)(2-i/2)&&p->pwrlevels[i].bus_min==1&&p->pwrlevels[i].bus_max==p->pwrlevels[i].bus_freq,"peak-only triple regression");}
  else {
   check(p->ddr_table_count==table_len&&!memcmp(p->ddr_table,table,table_len*sizeof(*table)),"indexed table was collapsed or rescaled");
   for(int i=0;i<7;i++){int j=0;while(j<row_count&&rows[j].freq!=p->pwrlevels[i].gpu_freq)j++;check(j<row_count,"golden frequency missing");check(p->pwrlevels[i].bus_freq==rows[j].def&&p->pwrlevels[i].bus_min==rows[j].min&&p->pwrlevels[i].bus_max==rows[j].max,"indexed triple mismatch");check(p->pwrlevels[i].gpu_freq==before.pwrlevels[i].gpu_freq&&p->pwrlevels[i].voltage_level==before.pwrlevels[i].voltage_level&&p->pwrlevels[i].acd_level==before.pwrlevels[i].acd_level,"OPP frequency/voltage/ACD overwritten");}
   u32 boot=table[p->pwrlevels[p->default_pwrlevel].bus_min];
   check(!kgsl_bus_vote_bootstrap(&gpu,true)&&last_peak==boot,"bootstrap did not use actual minimum");
   icc_vote_error=-EIO;check(kgsl_bus_vote_bootstrap(&gpu,false)==-EIO&&p->bootstrap_ib==boot&&p->icc_dirty,"bootstrap/ICC failure rollback regression");
   icc_vote_error=0;check(!kgsl_bus_vote_bootstrap(&gpu,false)&&!p->bootstrap_ib,"bootstrap release regression");
   u32 ab;p->bus_percent_ab=30;check(!_ab_buslevel_update(p,table[table_len-1],false,&ab)&&ab==(u64)30*table[table_len-1]/100000,"AB arithmetic changed");
  }
  kgsl_bus_close(&gpu);check(allocations==frees&&icc_puts==1,"successful close ownership leak");
 }
 if(!expected&&ret)check(false,"valid policy not loaded");
 if(!owned&&ret==0&&expected)kgsl_bus_close(&gpu);
 printf("%s: ret=%d expected=%d failures=%d alloc=%d free=%d opp_get=%d opp_put=%d icc_get=%d\n",argv[1],ret,expected,failures,allocations,frees,opp_gets,opp_puts,icc_gets);
 return failures?1:0;
}
