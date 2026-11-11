/* Actual loader and A6xx start function; firmware, SCM, memory and MMIO mocked. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
typedef uint32_t u32; typedef uint64_t phys_addr_t;
#define EPROBE_DEFER 517
#define SZ_4K 4096
#define IS_ALIGNED(v,a) (!((v)&((a)-1)))
#define MEMREMAP_WC 1
#define dev_err(...) ((void)0)
#define lockdep_assert_held(m) assert(*(m)==1)
#define ERR_PTR(v) ((void *)(intptr_t)(v))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define IS_ERR(p) ((uintptr_t)(p)>=(uintptr_t)-4095)
struct device_node {int unused;};
struct device {struct device_node *of_node;};
struct firmware {int unused;};
struct resource {uint64_t start,end;};
#define resource_size(r) ((r)->end-(r)->start+1)
struct memdesc {void *hostptr;uint64_t gpuaddr;};
struct kgsl_device {struct device *dev;struct {bool secured;} mmu;int mutex;void *scratch;};
struct adreno_ringbuffer {struct memdesc *buffer_desc;unsigned id,wptr,_wptr,wptr_preempt_end;};
struct adreno_device {struct kgsl_device dev;unsigned zap_loaded;int zap_load_error;struct adreno_ringbuffer rb;};
struct adreno_a6xx_core {const char *zap_name;};
#define KGSL_DEVICE(a) (&(a)->dev)
#define A6XX_CP_RB_CNTL_DEFAULT 0x100
#define ADRENO_APRIV 1
#define ADRENO_FEATURE(a,f) false
#define KGSL_RB_SIZE 16
#define FOR_EACH_RINGBUFFER(a,r,i) for((i)=0,(r)=&(a)->rb;(i)<1;++(i))
#define ADRENO_CURRENT_RINGBUFFER(a) (&(a)->rb)
#define SCRATCH_RPTR_OFFSET(i) ((i)*4)
#define SCRATCH_RPTR_GPU_ADDR(d,i) 0x12000ULL
#define lower_32_bits(v) ((u32)(v))
#define upper_32_bits(v) ((u32)((v)>>32))
enum {A6XX_CP_RB_RPTR_ADDR_LO,A6XX_CP_RB_RPTR_ADDR_HI,A6XX_CP_RB_CNTL,A6XX_CP_RB_BASE,A6XX_CP_RB_BASE_HI,A6XX_RBBM_SECVID_TRUST_CNTL,CP_SET_SECURE_MODE};
static struct device_node root,node;
static struct device dev={&root};
static struct firmware fw;
static char image[8192],ring[KGSL_RB_SIZE];
static struct memdesc mem={ring,0x10000};
static struct adreno_a6xx_core core={"a640_zap"};
static struct resource resource;
static bool missing_node,available,scm_available,map_fail;
static const char *status,*fwname;
static int property_error,region_error,request_error,load_error,auth_error,shutdown_error;
static int cp_error,alloc_error,submit_error,post_error;
static ssize_t image_size;
static unsigned gets,node_puts,requests,releases,maps,unmaps,loads,auths,shutdowns,trust_writes,submits,posts;
static unsigned commands[2];
static struct device_node *of_get_child_by_name(struct device_node *parent,const char *name) {assert(parent==&root && !strcmp(name,"zap-shader"));if(missing_node)return NULL;++gets;return &node;}
static bool of_device_is_available(struct device_node *n) {assert(n==&node);return available;}
static int of_property_read_string(struct device_node *n,const char *key,const char **value) {assert(n==&node);if(property_error)return property_error;*value=!strcmp(key,"status")?status:fwname;return 0;}
static bool qcom_scm_is_available(void) {return scm_available;}
static int of_reserved_mem_region_to_resource(struct device_node *n,unsigned i,struct resource *r) {assert(n==&node && i==0);*r=resource;return region_error;}
static void of_node_put(struct device_node *n) {assert(n==&node && node_puts<gets);++node_puts;}
static int request_firmware_direct(const struct firmware **f,const char *name,struct device *d) {assert(d==&dev && !strcmp(name,"qcom/msft/surface/pro-x-sq2/qcdxkmsuc8180.mbn"));++requests;if(!request_error)*f=&fw;return request_error;}
static void release_firmware(const struct firmware *f) {assert(f==&fw && releases<requests);++releases;}
static ssize_t qcom_mdt_get_size(const struct firmware *f) {assert(f==&fw);return image_size;}
static void *memremap(phys_addr_t addr,size_t size,unsigned flags) {assert(addr==resource.start && size==(size_t)image_size && flags==MEMREMAP_WC);++maps;return map_fail?NULL:image;}
static void memunmap(void *p) {assert(p==image && unmaps<maps);++unmaps;}
static int qcom_mdt_load(struct device *d,const struct firmware *f,const char *name,unsigned pas,void *memory,phys_addr_t addr,size_t size,void *reloc) {assert(d==&dev && f==&fw && !strcmp(name,fwname) && pas==13 && memory==image && addr==resource.start && size==(size_t)image_size && !reloc);++loads;return load_error;}
static int qcom_scm_pas_auth_and_reset(unsigned pas) {assert(pas==13 && loads && !load_error);++auths;return auth_error;}
static int qcom_scm_pas_shutdown(unsigned pas) {assert(pas==13 && loads);++shutdowns;return shutdown_error;}
static const struct adreno_a6xx_core *to_a6xx_core(struct adreno_device *a) {return &core;}
static void kgsl_sharedmem_writel(void *s,unsigned offset,unsigned value) {assert(!value);}
static void a6xx_preemption_start(struct adreno_device *a) {}
static void kgsl_regwrite(struct kgsl_device *d,unsigned reg,u32 value) {if(reg==A6XX_RBBM_SECVID_TRUST_CNTL){assert(!value);++trust_writes;}}
static void a6xx_unhalt_sqe(struct adreno_device *a) {}
static int a6xx_send_cp_init(struct adreno_device *a,struct adreno_ringbuffer *rb) {return cp_error;}
static unsigned *adreno_ringbuffer_allocspace(struct adreno_ringbuffer *rb,int n) {assert(n==2);return alloc_error?ERR_PTR(alloc_error):commands;}
static unsigned cp_packet(struct adreno_device *a,unsigned opcode,unsigned n) {assert(opcode==CP_SET_SECURE_MODE && n==1 && a->zap_loaded);return 0x123456;}
static int adreno_ringbuffer_submit_spin(struct adreno_ringbuffer *rb,void *p,unsigned timeout) {assert(commands[0]==0x123456 && commands[1]==0 && timeout==2000);++submits;return submit_error;}
static void a6xx_spin_idle_debug(struct adreno_device *a,const char *msg) {}
static int a6xx_post_start(struct adreno_device *a) {++posts;return post_error;}
#include "kgsl-zap-functions.h"
static struct adreno_device reset(void) {
 missing_node=map_fail=false;available=scm_available=true;status="okay";fwname="qcom/msft/surface/pro-x-sq2/qcdxkmsuc8180.mbn";
 property_error=region_error=request_error=load_error=auth_error=shutdown_error=cp_error=alloc_error=submit_error=post_error=0;
 gets=node_puts=requests=releases=maps=unmaps=loads=auths=shutdowns=trust_writes=submits=posts=0;
 resource=(struct resource){0x90000000,0x90001fff};image_size=4096;core.zap_name="a640_zap";
 return (struct adreno_device){.dev={.dev=&dev,.mutex=1},.rb={.buffer_desc=&mem}};
}
int main(void) {
 unsigned cases=0;
 for(int scenario=0;scenario<25;++scenario) {
  struct adreno_device a=reset();int want=0;
  if(scenario==1){missing_node=true;want=-ENOENT;}
  if(scenario==2){available=false;status="disabled";}
  if(scenario==3){available=false;status="disabled";a.dev.mmu.secured=true;want=-EOPNOTSUPP;}
  if(scenario==4){available=false;status="reserved";want=-ENODEV;}
  if(scenario==5){available=false;property_error=-EINVAL;want=-EINVAL;}
  if(scenario==6){scm_available=false;want=-EPROBE_DEFER;}
  if(scenario==7){region_error=-ENOENT;want=-ENOENT;}
  if(scenario==8){resource.end=resource.start-1;want=-EINVAL;}
  if(scenario==9){++resource.start;want=-EINVAL;}
  if(scenario==10){property_error=-ENOENT;want=-ENOENT;}
  if(scenario==11){fwname="";want=-EINVAL;}
  if(scenario==12){request_error=-ENOENT;want=-ENOENT;}
  if(scenario==13){image_size=-EINVAL;want=-EINVAL;}
  if(scenario==14){image_size=0;want=-EINVAL;}
  if(scenario==15){image_size=12288;want=-E2BIG;}
  if(scenario==16){map_fail=true;want=-ENOMEM;}
  if(scenario==17){load_error=-ENODEV;want=-ENODEV;}
  if(scenario==18){auth_error=-EACCES;want=-EACCES;}
  if(scenario==19){auth_error=-EOPNOTSUPP;want=-EOPNOTSUPP;}
  if(scenario==20){auth_error=-ETIMEDOUT;shutdown_error=-EBUSY;want=-ETIMEDOUT;}
  if(scenario==21){cp_error=-EIO;want=-EIO;}
  if(scenario==22){alloc_error=-ENOSPC;want=-ENOSPC;}
  if(scenario==23){submit_error=-ETIMEDOUT;want=-ETIMEDOUT;}
  if(scenario==24){post_error=-EIO;want=-EIO;}
  assert(a6xx_rb_start(&a)==want);
  assert(gets==node_puts && releases==requests-(request_error?1:0) && unmaps==maps-(map_fail?1:0));
  assert(trust_writes==(scenario==2));
  bool loaded=scenario==0 || scenario>=22;
  assert(!!a.zap_loaded==loaded);
  assert(posts==(scenario==0 || scenario==2 || scenario==24));
  if(scenario>=17 && scenario<=20)assert(loads==1 && shutdowns==1 && !trust_writes && !submits && !posts);
  if(scenario==20){unsigned saved=gets;assert(a.zap_load_error==-EBUSY);assert(a6xx_rb_start(&a)==-EBUSY && gets==saved && requests==1 && shutdowns==1);++cases;}
  if(scenario==0){assert(!a6xx_rb_start(&a));assert(requests==1 && loads==1 && auths==1 && submits==2 && !trust_writes);++cases;}
  ++cases;
 }
 for(int empty=0;empty<2;++empty) {struct adreno_device a=reset();core.zap_name=empty?"":NULL;assert(a6xx_rb_start(&a)==-ENOENT && !gets && !trust_writes && !posts);++cases;}
 printf("%u ZAP/A6xx start scenarios passed (firmware, PAS, reserved memory and MMIO mocked)\n",cases);return 0;
}
