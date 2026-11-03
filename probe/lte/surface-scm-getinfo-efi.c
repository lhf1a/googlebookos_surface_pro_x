/* SPDX-License-Identifier: BSD-3-Clause */
/* Single GET_INFO, no INIT/READ/WRITE/ERASE/DEINIT or arbitrary command input.
 * Synchronous firmware calls cannot be preempted by a software time limit:
 * bounded means one invocation/two pages, NOT a guaranteed elapsed deadline.
 * Not connected to normal boot. A future one-shot wrapper must first verify
 * the physical Surface/SC8180X target and preserve its original launcher.
 */
#include "surface-scm-getinfo-efi.h"
#include "external/CacheMaintenanceLib.h"

_Static_assert(sizeof(UINTN)==8 && sizeof(void*)==8, "64-bit EFI only");
_Static_assert(EFI_PAGE_SIZE==SPX_STORAGE_PAGE, "page ABI");
_Static_assert(SCM_MAX_NUM_PARAMETERS==10 && SCM_MAX_NUM_RESULTS==4, "SCM slots");
_Static_assert(offsetof(QCOM_SCM_PROTOCOL,ScmSipSysCall)==56, "pinned EFI vtable");

/* Only the offline harness supplies a replacement; release uses CurrentEL. */
#ifndef SPX_GETINFO_TEST_EL
static UINT32 spx_current_el(void) {
 UINT64 el; __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
 return (UINT32)((el>>2)&3);
}
#else
static UINT32 spx_current_el(void) { return SPX_GETINFO_TEST_EL; }
#endif

/* Invalidate only after SCM, never clean potentially stale output back over
 * firmware's stores. The pinned cache library supplies the pre-call clean.
 */
#ifndef SPX_GETINFO_TEST_CACHE
static void spx_invalidate(void *address,UINTN size) {
 UINT64 ctr; __asm__ volatile("mrs %0, CTR_EL0" : "=r"(ctr));
 UINTN line=(UINTN)4<<((ctr>>16)&15), p=(UINTN)address;
 __asm__ volatile("dsb sy" ::: "memory");
 for(UINTN end=p+size;p<end;p+=line)
  __asm__ volatile("dc ivac, %0" :: "r"(p) : "memory");
 __asm__ volatile("dsb sy" ::: "memory");
}
#else
static void spx_invalidate(void *address,UINTN size) { SPX_GETINFO_TEST_CACHE(address,size); }
#endif

EFI_STATUS spx_scm_get_info(EFI_BOOT_SERVICES *bs,QCOM_SCM_PROTOCOL *scm,
                          struct spx_scm_info_report *report) {
 if(!report)return EFI_INVALID_PARAMETER;
 memset(report,0,sizeof(*report));
 report->allocation_status=report->call_status=report->free_status=EFI_NOT_READY;
 report->check=SPX_INFO_ARGUMENT;
 for(unsigned i=0;i<SCM_MAX_NUM_RESULTS;i++)report->results[i]=UINT64_MAX;
 report->current_el=spx_current_el();
 if(scm)report->revision=scm->Revision;
 if(!bs || !bs->AllocatePages || !bs->FreePages || !scm || !scm->ScmSipSysCall)
  return EFI_INVALID_PARAMETER;
 if(report->current_el!=1 || scm->Revision!=QCOM_SCM_PROTOCOL_REVISION)
  return EFI_UNSUPPORTED;

 /* Owned, contiguous pages below 4GiB; fail closed instead of truncating PA.
  * The function must run before ExitBootServices/SetVirtualAddressMap.
  */
 EFI_PHYSICAL_ADDRESS base=0xffffffff;
 EFI_STATUS status=bs->AllocatePages(AllocateMaxAddress,EfiBootServicesData,2,&base);
 report->allocation_status=status;
 if(status!=EFI_SUCCESS)return EFI_ERROR(status) ? status : EFI_DEVICE_ERROR;
 if(!base || base%SPX_STORAGE_PAGE || base>0xffffe000) {
  report->free_status=bs->FreePages(base,2);
  return EFI_INVALID_PARAMETER;
 }
 UINT8 *pages=(void *)(UINTN)base, expected[64];
 struct spx_storage_descriptor desc;
 UINT64 args[SCM_MAX_NUM_PARAMETERS]={0};
 memset(pages,0xa5,2*SPX_STORAGE_PAGE);
 if(!spx_storage_encode(pages,SPX_STORAGE_PAGE,&desc,base,SPX_STORAGE_GET_INFO,0,NULL)) {
  status=EFI_INVALID_PARAMETER; goto cleanup;
 }
 memcpy(expected,pages,sizeof(expected));
 for(unsigned i=0;i<4;i++)args[i]=desc.args[i];
 WriteBackInvalidateDataCacheRange(pages,2*SPX_STORAGE_PAGE);
 report->calls=1;
 status=scm->ScmSipSysCall(scm,desc.command,desc.arginfo,args,report->results);
 report->call_status=status;
 spx_invalidate(pages,2*SPX_STORAGE_PAGE);
 report->returned_size=spx_storage_u64(pages+64);
 if(status==EFI_SUCCESS) {
  report->check=spx_storage_check_info(pages,2*SPX_STORAGE_PAGE,expected,
                                     report->results,&report->geometry);
  if(report->check!=SPX_INFO_OK)status=EFI_COMPROMISED_DATA;
 }
cleanup:
 /* Do not leak the private GET_INFO identity fields to a later allocator. */
 memset(pages,0,2*SPX_STORAGE_PAGE);
 WriteBackInvalidateDataCacheRange(pages,2*SPX_STORAGE_PAGE);
 report->free_status=bs->FreePages(base,2);
 if(status==EFI_SUCCESS && report->free_status!=EFI_SUCCESS)status=report->free_status;
 report->candidate_valid=(status==EFI_SUCCESS);
 return status==EFI_SUCCESS || EFI_ERROR(status) ? status : EFI_DEVICE_ERROR;
}
