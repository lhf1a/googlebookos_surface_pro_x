/* SPDX-License-Identifier: BSD-3-Clause */
/* Real transport C + real EFI/protocol headers. Only the environment is mocked.
 * mmap supplies owned process pages; NO EFI firmware / secure-world execution.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>
static unsigned test_el=1;
static void invalidate_mock(void *,unsigned long);
#define SPX_GETINFO_TEST_EL test_el
#define SPX_GETINFO_TEST_CACHE invalidate_mock
#include "../probe/lte/surface-scm-getinfo-efi.c"

enum fault {GOOD,ALLOC_FAIL,BAD_PA,CALL_FAIL,CALL_WARN,UNTOUCHED,NOT_DONE,
 SERVICE_FAIL,SERVICE_HIGH_FAIL,REQUEST_CHANGED,META_GUARD,DATA_GUARD,
 SHORT_SIZE,LARGE_SIZE,BAD_GEOMETRY,FREE_FAIL,RETURN_SIZE,FAULT_COUNT};
static enum fault fault;
static unsigned allocations,calls,frees,cleans,invalidates;
static char order[32];static unsigned order_n;
static void *allocation;
static void event(char c) { assert(order_n<sizeof(order)-1);order[order_n++]=c; }
static EFI_STATUS EFIAPI allocate_mock(EFI_ALLOCATE_TYPE type,EFI_MEMORY_TYPE mem,
                                      UINTN n,EFI_PHYSICAL_ADDRESS *base) {
 event('A');allocations++;
 assert(type==AllocateMaxAddress && mem==EfiBootServicesData && n==2);
 assert(*base==0xffffffff);
 if(fault==ALLOC_FAIL)return EFI_OUT_OF_RESOURCES;
 if(fault==BAD_PA){*base=0x100000000ULL;return EFI_SUCCESS;}
 allocation=mmap((void*)0x31000000,8192,PROT_READ|PROT_WRITE,
                 MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
 assert(allocation!=(void*)-1 && (uintptr_t)allocation==0x31000000);
 *base=(uintptr_t)allocation;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI free_mock(EFI_PHYSICAL_ADDRESS base,UINTN n) {
 event('F');frees++;assert(n==2);
 if(fault==BAD_PA){assert(base==0x100000000ULL);return EFI_SUCCESS;}
 assert((void*)(uintptr_t)base==allocation);
 for(unsigned i=0;i<8192;i++)assert(((unsigned char*)allocation)[i]==0);
 assert(!munmap(allocation,8192));allocation=NULL;
 return fault==FREE_FAIL ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}
VOID *EFIAPI WriteBackInvalidateDataCacheRange(VOID *p,UINTN n) {
 event('C');cleans++;assert(p==allocation && n==8192);return p;
}
static void invalidate_mock(void *p,unsigned long n) {
 event('I');invalidates++;assert(p==allocation && n==8192 && calls==1);
}
static EFI_STATUS EFIAPI call_mock(QCOM_SCM_PROTOCOL *scm,UINT32 command,UINT32 arginfo,
                                  UINT64 args[10],UINT64 results[4]) {
 (void)scm;event('S');calls++;
 assert(cleans==1 && !invalidates && !frees && calls==1);
 assert(command==0x02001a01 && arginfo==0x214);
 for(unsigned i=4;i<10;i++)assert(args[i]==0);
 for(unsigned i=0;i<4;i++)assert(results[i]==UINT64_MAX);
 uint8_t *p=allocation;
 assert(args[0]==(uintptr_t)p && args[1]==40 && args[2]==(uintptr_t)(p+40) && args[3]==32);
 assert(spx_storage_u64(p)==1 && !spx_storage_u64(p+8) && !spx_storage_u64(p+16));
 assert(!spx_storage_u64(p+24) && spx_storage_u64(p+32)==4 && !spx_storage_u64(p+40));
 assert(spx_storage_u64(p+48)==105 && spx_storage_u64(p+56)==(uintptr_t)(p+4096));
 assert(!spx_storage_u64(p+64));
 if(fault==CALL_FAIL)return EFI_DEVICE_ERROR;
 if(fault==CALL_WARN)return EFI_WARN_UNKNOWN_GLYPH;
 if(fault==UNTOUCHED)return EFI_SUCCESS;
 results[0]=1;results[1]=0;
 memset(p+4096,0,105);spx_storage_put64(p+4096,8192);
 p[4096+9]=16;p[4096+13]=1;p[4096+16]=1;
 switch(fault) {
 case NOT_DONE:results[0]=0;break;
 case SERVICE_FAIL:results[1]=UINT64_MAX;break;
 case SERVICE_HIGH_FAIL:results[1]=1ULL<<32;break;
 case REQUEST_CHANGED:p[56]^=1;break;
 case META_GUARD:p[72]^=1;break;
 case DATA_GUARD:p[4201]^=1;break;
 case SHORT_SIZE:spx_storage_put64(p+64,104);break;
 case LARGE_SIZE:spx_storage_put64(p+64,106);break;
 case BAD_GEOMETRY:p[4096+9]=2;break;
 case RETURN_SIZE:spx_storage_put64(p+64,105);break;
 default:break;
 }
 return EFI_SUCCESS;
}
static void reset(void) {
 allocations=calls=frees=cleans=invalidates=order_n=0;memset(order,0,sizeof(order));
}
int main(void) {
 EFI_BOOT_SERVICES bs={.AllocatePages=allocate_mock,.FreePages=free_mock};
 QCOM_SCM_PROTOCOL scm={.Revision=QCOM_SCM_PROTOCOL_REVISION,.ScmSipSysCall=call_mock};
 struct spx_scm_info_report report;
 for(fault=GOOD;fault<FAULT_COUNT;fault++) {
  reset();EFI_STATUS status=spx_scm_get_info(&bs,&scm,&report);
  int success=(fault==GOOD || fault==RETURN_SIZE);
  assert((status==EFI_SUCCESS)==success && report.candidate_valid==(unsigned)success);
  if(!success)assert(EFI_ERROR(status));
  if(fault==ALLOC_FAIL)assert(!strcmp(order,"A") && !calls && !frees);
  else if(fault==BAD_PA)assert(!strcmp(order,"AF") && !calls && frees==1);
  if(fault!=ALLOC_FAIL && fault!=BAD_PA) {
   assert(!strcmp(order,"ACSICF"));
   assert(calls==1 && frees==1 && invalidates==1 && cleans==2 && report.calls==1);
  }
  if(success)assert(report.geometry.blocks==8192 && report.geometry.block_bytes==4096);
  if(fault==SERVICE_HIGH_FAIL)assert(report.results[1]==(1ULL<<32));
  printf("mock_case=%u status=%llx calls=%u valid=%u order=%s\n",
         fault,(unsigned long long)status,calls,report.candidate_valid,order);
 }
 fault=GOOD;
 for(test_el=0;test_el<4;test_el++) {
  if(test_el==1)continue;
  reset();assert(spx_scm_get_info(&bs,&scm,&report)==EFI_UNSUPPORTED && !allocations && !calls);
 }
 test_el=1;scm.Revision=0;reset();
 assert(spx_scm_get_info(&bs,&scm,&report)==EFI_UNSUPPORTED && !allocations);
 assert(spx_scm_get_info(NULL,&scm,&report)==EFI_INVALID_PARAMETER);
 assert(spx_scm_get_info(&bs,NULL,&report)==EFI_INVALID_PARAMETER);
 assert(spx_scm_get_info(&bs,&scm,NULL)==EFI_INVALID_PARAMETER);
 scm.Revision=QCOM_SCM_PROTOCOL_REVISION;scm.ScmSipSysCall=NULL;
 assert(spx_scm_get_info(&bs,&scm,&report)==EFI_INVALID_PARAMETER && !allocations);
 puts("PASS real transport + pinned ABI; environment mocks only; NO hardware calls");
 return 0;
}
