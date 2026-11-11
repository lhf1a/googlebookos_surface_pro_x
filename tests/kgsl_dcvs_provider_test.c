#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
typedef uint32_t u32; typedef uint64_t u64; typedef int32_t s32;
#define PAGE_SIZE 4096
#define U32_MAX UINT32_MAX
#define GFP_KERNEL 0
#define EPROBE_DEFER 517
#define ARM_SMCCC_OWNER_SIP 2
#define EXPORT_SYMBOL_GPL(x)
#include "scm-types.h"
struct qcom_scm {void *dev,*mempool;};
static struct qcom_scm state;
static struct qcom_scm *__scm;
static unsigned missing,checks,allocs,frees,calls,barriers;
static int convention,error;
static bool alloc_fail,phys_fail;
static u64 response;
static void *buffer;
static size_t bytes;
static struct qcom_scm_desc last;
static int __get_convention(void) {return convention;}
static bool __qcom_scm_is_call_available(void *dev,u32 svc,u32 cmd) {assert(svc==0x0d);++checks;return cmd!=missing;}
static void *qcom_tzmem_alloc(void *pool,size_t n,int flags) {++allocs;bytes=n;return buffer=alloc_fail?NULL:calloc(1,n);}
static u64 qcom_tzmem_to_phys(void *p) {assert(p==buffer);return phys_fail?0:0x12345678000ULL;}
static void qcom_tzmem_free(void *p) {assert(p==buffer);++frees;free(p);buffer=NULL;}
static void wmb(void) {++barriers;}
static int qcom_scm_call(void *dev,const struct qcom_scm_desc *d,struct qcom_scm_res *res) {
 ++calls;last=*d;assert(d->svc==0x0d && d->owner==2);
 if(d->cmd==0x0b || d->cmd==0x0c) {
  assert(buffer && barriers==1 && d->args[0]==0x12345678000ULL && d->args[1]==bytes);
  assert(d->arginfo==(2 | (2<<4)));
  const u32 *words=buffer;
  if(d->cmd==0x0b)assert(bytes==16 && words[0]==3 && words[1]==600000000 && words[2]==400000000 && words[3]==200000000);
  else assert(bytes==8 && words[0]==2 && words[1]==7);
 }
 if(res)res->result[0]=response;
 return error;
}
#include "provider-functions.h"
static void reset(void) {assert(!buffer);__scm=&state;convention=SMC_CONVENTION_ARM_64;missing=checks=allocs=frees=calls=barriers=0;error=0;alloc_fail=phys_fail=false;response=0;}
int main(void) {
 unsigned cases=0;u32 levels[]={3,600000000,400000000,200000000};u32 version; s32 delta;
 for(int fail=0;fail<9;++fail) {
  reset();version=99;
  int want=0;
  if(fail==1){__scm=NULL;want=-EPROBE_DEFER;}
  if(fail==2){convention=SMC_CONVENTION_ARM_32;want=-EOPNOTSUPP;}
  if(fail>=3 && fail<=5){missing=fail==3?0x0b:fail==4?0x0a:0x07;want=-EOPNOTSUPP;}
  if(fail==6){alloc_fail=true;want=-ENOMEM;}
  if(fail==7){phys_fail=true;want=-EFAULT;}
  if(fail==8){error=-ETIMEDOUT;want=error;}
  assert(qcom_scm_gpu_dcvs_init(levels,4,&version)==want);
  assert(version==(want?99:0) && !buffer);
  assert(frees==(!alloc_fail && allocs));++cases;
 }
 reset();response=1ULL<<32;version=99;assert(qcom_scm_gpu_dcvs_init(levels,4,&version)==-EOVERFLOW && version==99 && frees==1);++cases;
 for(int bad=0;bad<5;++bad) {reset();u32 invalid[]={2,1};version=99;
  assert(qcom_scm_gpu_dcvs_init(bad==0?NULL:bad==4?invalid:levels,bad==1?1:bad==2?1025:bad==4?2:4,bad==3?NULL:&version)==-EINVAL);
  assert(!calls && !allocs && !checks && version==99);++cases;
 }
 for(int fail=0;fail<5;++fail) {
  reset();int want=0;
  if(fail==1 || fail==2){missing=fail==1?0x0c:0x0d;want=-EOPNOTSUPP;}
  if(fail==3){error=-EBUSY;want=error;}
  if(fail==4){alloc_fail=true;want=-ENOMEM;}
  assert(qcom_scm_gpu_dcvs_init_ca(2,7)==want && !buffer);assert(frees==(!alloc_fail && allocs));++cases;
 }
 for(int ca=0;ca<2;++ca) for(int fail=0;fail<4;++fail) {
  reset();delta=123;response=fail==0?UINT32_MAX:2;error=fail==2?-EBUSY:fail==3?-EIO:0;
  assert(qcom_scm_gpu_dcvs_update(2,1ULL<<40,12345,ca,8,&delta)==error);
  assert(delta==(error?123:fail==0?-1:2));
  assert(last.cmd==(ca?0x0d:0x0a) && last.arginfo==(ca?4:3));
  assert(last.args[0]==2 && last.args[1]==(1ULL<<40) && last.args[2]==12345 && last.args[3]==8);++cases;
 }
 for(int bad=0;bad<4;++bad) {reset();delta=123;if(bad==3)convention=SMC_CONVENTION_ARM_32;
  assert(qcom_scm_gpu_dcvs_update(0,bad==0?0:10,bad==1?11:0,false,0,bad==2?NULL:&delta)==(bad==3?-EOPNOTSUPP:-EINVAL));assert(delta==123 && !calls);++cases;
 }
 for(int fail=0;fail<3;++fail) {reset();error=fail==1?-EBUSY:0;if(fail==2)__scm=NULL;
  assert(qcom_scm_gpu_dcvs_reset()==(fail==2?-EPROBE_DEFER:error));if(fail!=2)assert(calls==1 && last.cmd==7 && !last.arginfo);++cases;
 }
 printf("%u SCM DCVS scenarios passed (firmware and coherent pool mocked)\n",cases);return 0;
}
