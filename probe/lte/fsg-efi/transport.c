/* SPDX-License-Identifier: BSD-3-Clause */
/* Legacy GET_INFO retained. Optional primary-GPT metadata reader below adds
 * bounded READs only; no INIT/WRITE/ERASE/DEINIT or arbitrary command input.
 * Synchronous firmware calls cannot be preempted by a software time limit:
 * bounded means one invocation/two pages, NOT a guaranteed elapsed deadline.
 * Used by the default pre-EL2 own-device FSG cache loader.
 */
#include "surface-scm-getinfo-efi.h"
#include "bridge-prefix.h"
#include "external/CacheMaintenanceLib.h"

_Static_assert(sizeof(UINTN)==8 && sizeof(void*)==8, "64-bit EFI only");
_Static_assert(EFI_PAGE_SIZE==SPX_STORAGE_PAGE, "page ABI");
_Static_assert(SCM_MAX_NUM_PARAMETERS==10 && SCM_MAX_NUM_RESULTS==4, "SCM slots");
_Static_assert(offsetof(QCOM_SCM_PROTOCOL,ScmSipSysCall)==56, "pinned EFI vtable");

static UINT32 spx_current_el(void) {
 UINT64 el; __asm__ volatile("mrs %0, CurrentEL" : "=r"(el));
 return (UINT32)((el>>2)&3);
}


/* Invalidate only after SCM, never clean potentially stale output back over
 * firmware's stores. The pinned cache library supplies the pre-call clean.
 */
static void spx_invalidate(void *address,UINTN size) {
 UINT64 ctr; __asm__ volatile("mrs %0, CTR_EL0" : "=r"(ctr));
 UINTN line=(UINTN)4<<((ctr>>16)&15), p=(UINTN)address;
 __asm__ volatile("dsb sy" ::: "memory");
 for(UINTN end=p+size;p<end;p+=line)
  __asm__ volatile("dc ivac, %0" :: "r"(p) : "memory");
 __asm__ volatile("dsb sy" ::: "memory");
}


EFI_STATUS spx_scm_get_info(EFI_BOOT_SERVICES *bs,QCOM_SCM_PROTOCOL *scm,
                          struct spx_scm_info_report *report) {
 if(!report)return EFI_INVALID_PARAMETER;
 memset(report,0,sizeof(*report));
 report->allocation_status=report->call_status=report->free_status=EFI_NOT_READY;
 report->check=SPX_INFO_ARGUMENT;
 for(unsigned i=0;i<SCM_MAX_NUM_RESULTS;i++)report->results[i]=UINT64_MAX;
 report->current_el=spx_current_el();
 if(scm)report->revision=scm->Revision;
 if(!bs || !bs->LocateProtocol || !scm || !scm->ScmSipSysCall)
  return EFI_INVALID_PARAMETER;
 if(report->current_el!=1 || (scm->Revision!=QCOM_SCM_PROTOCOL_REVISION && scm->Revision!=0x10002ULL))
  return EFI_UNSUPPORTED;

 /* Use the existing registered EFI SHM allocator, not ordinary pages.
  * Unknown/missing protocol fails closed; never fall back to AllocatePages.
  * No explicit bridge create/delete, VM assignment, INIT or storage write.
  */
 EFI_GUID guid={0x9c1eb71f,0xdd6c,0x4ed5,{0x9f,0x6a,0x5c,0xc0,0xca,0x78,0x9f,0x16}};
 struct spx_shm_prefix *bridge=NULL;
 EFI_STATUS status=bs->LocateProtocol(&guid,NULL,(VOID **)&bridge);
 report->allocation_status=status;
 if(status!=EFI_SUCCESS)return EFI_ERROR(status)?status:EFI_DEVICE_ERROR;
 if(!bridge || bridge->Revision!=0x10001ULL || !bridge->Allocate || !bridge->Free) {
  report->allocation_status=EFI_UNSUPPORTED;
  return EFI_UNSUPPORTED;
 }
 UINT64 allocation_bytes=2*SPX_STORAGE_PAGE;
 VOID *owned=bridge->Allocate(bridge,&allocation_bytes,SPX_SHM_BOOT);
 report->allocation_status=owned?EFI_SUCCESS:EFI_OUT_OF_RESOURCES;
 if(!owned)return EFI_OUT_OF_RESOURCES;
 EFI_PHYSICAL_ADDRESS base=(UINTN)owned;
 if(allocation_bytes!=2*SPX_STORAGE_PAGE || !base || base%SPX_STORAGE_PAGE || base>0xffffe000) {
  report->free_status=bridge->Free(bridge,owned);
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
 report->free_status=bridge->Free(bridge,owned);
 if(status==EFI_SUCCESS && report->free_status!=EFI_SUCCESS)status=report->free_status;
 report->candidate_valid=(status==EFI_SUCCESS);
 return status==EFI_SUCCESS || EFI_ERROR(status) ? status : EFI_DEVICE_ERROR;
}

#define SPX_PRIMARY_GPT_V1 1
#include "primary-gpt.h"
/* Internal READ only. No caller-selected storage type, operation or partition. */
static EFI_STATUS spx_read_metadata_block(QCOM_SCM_PROTOCOL *scm,UINT8 *pages,
 const struct spx_storage_geometry *g,UINT64 lba,UINT8 *dst,struct spx_gpt_report *r) {
 struct spx_storage_descriptor d;UINT8 expected[64];UINT64 args[10]={0};
 memset(pages,0xa5,8192);
 if(!spx_storage_encode(pages,4096,&d,(UINTN)pages,SPX_STORAGE_READ,lba,g))return EFI_INVALID_PARAMETER;
 memcpy(expected,pages,64);for(unsigned i=0;i<4;i++){args[i]=d.args[i];r->results[i]=UINT64_MAX;}
 WriteBackInvalidateDataCacheRange(pages,8192);r->last_lba=lba;r->reads++;
 EFI_STATUS s=scm->ScmSipSysCall(scm,d.command,d.arginfo,args,r->results);r->call_status=s;
 spx_invalidate(pages,8192);r->returned_size=spx_storage_u64(pages+64);
 if(s!=EFI_SUCCESS)return EFI_ERROR(s)?s:EFI_DEVICE_ERROR;
 if(r->results[0]!=1 || r->results[1]!=0 || memcmp(pages,expected,64) ||
    (r->returned_size!=0 && r->returned_size!=4096))return EFI_COMPROMISED_DATA;
 for(unsigned i=72;i<4096;i++)if(pages[i]!=0xa5)return EFI_COMPROMISED_DATA;
 memcpy(dst,pages+4096,4096);return EFI_SUCCESS;
}
/* One GET_INFO, fixed READ0/READ1, then <=4 metadata READs from a CRC-valid
 * bounded primary GPT. No backup fallback or FSG-content read. No disk writes.
 * Caller owns the whole report; do not place its24KiB arrays on a small stack.
 * Synchronous SCM may hang: call-count bounds are NOT elapsed-time guarantees.
 */
EFI_STATUS spx_scm_primary_gpt(EFI_BOOT_SERVICES *bs,QCOM_SCM_PROTOCOL *scm,
 struct spx_gpt_report *r) {
 if(!r)return EFI_INVALID_PARAMETER;
 memset(r,0,sizeof(*r));
 r->allocation_status=r->call_status=r->free_status=EFI_NOT_READY;r->stage=1;
 EFI_STATUS s=spx_scm_get_info(bs,scm,&r->info);r->status=s;if(s!=EFI_SUCCESS)return s;
 EFI_GUID guid={0x9c1eb71f,0xdd6c,0x4ed5,{0x9f,0x6a,0x5c,0xc0,0xca,0x78,0x9f,0x16}};
 struct spx_shm_prefix *bridge=NULL;s=bs->LocateProtocol(&guid,NULL,(VOID**)&bridge);r->allocation_status=s;
 if(s!=EFI_SUCCESS){r->status=EFI_ERROR(s)?s:EFI_DEVICE_ERROR;return r->status;}
 if(!bridge || bridge->Revision!=0x10001ULL || !bridge->Allocate || !bridge->Free){r->status=EFI_UNSUPPORTED;return r->status;}
 UINT64 n=8192;VOID *owned=bridge->Allocate(bridge,&n,SPX_SHM_BOOT);
 r->allocation_status=owned?EFI_SUCCESS:EFI_OUT_OF_RESOURCES;
 if(!owned){r->status=EFI_OUT_OF_RESOURCES;return r->status;}
 if(n!=8192 || !(UINTN)owned || (UINTN)owned%4096 || (UINTN)owned>0xffffe000){r->free_status=bridge->Free(bridge,owned);r->status=EFI_INVALID_PARAMETER;return r->status;}
 UINT8 *pages=owned;r->stage=2;
 s=spx_read_metadata_block(scm,pages,&r->info.geometry,0,r->mbr,r);if(s!=EFI_SUCCESS)goto done;
 if(!spx_gpt_mbr(r->mbr,&r->info.geometry)){s=EFI_COMPROMISED_DATA;goto done;}
 r->stage=3;s=spx_read_metadata_block(scm,pages,&r->info.geometry,1,r->header,r);if(s!=EFI_SUCCESS)goto done;
 if(!spx_gpt_header(r->header,&r->info.geometry,&r->plan)){s=EFI_COMPROMISED_DATA;goto done;}
 r->stage=4;
 for(unsigned i=0;i<r->plan.blocks;i++){s=spx_read_metadata_block(scm,pages,&r->info.geometry,r->plan.entries_lba+i,r->table+i*4096,r);if(s!=EFI_SUCCESS)goto done;}
 {unsigned found=0;uint64_t first=0,count=0;
  if(!spx_gpt_entries(r->table,&r->plan,&first,&count,&found)){s=EFI_COMPROMISED_DATA;goto done;}
  r->fsg_found=found;r->fsg_first=first;r->fsg_blocks=count;r->stage=5;
 }
 done:
 memset(pages,0,8192);WriteBackInvalidateDataCacheRange(pages,8192);r->free_status=bridge->Free(bridge,owned);
 if(s==EFI_SUCCESS && r->free_status!=EFI_SUCCESS)s=EFI_ERROR(r->free_status)?r->free_status:EFI_DEVICE_ERROR;
 r->status=s;r->valid=(s==EFI_SUCCESS);
 if(!r->valid){r->fsg_found=0;r->fsg_first=0;r->fsg_blocks=0;}
 return s;
}

#include "fsg-export.h"
/* Fixed FSG type GUID is resolved by a fresh CRC-checked own-unit GPT.
 * This exports bytes to caller memory only; no storage write or guessed LBA.
 * Output capacity is a memory bound, not a claimed universal partition size.
 */
EFI_STATUS spx_scm_fsg_export(EFI_BOOT_SERVICES *bs,QCOM_SCM_PROTOCOL *scm,
 struct spx_fsg_report *r) {
 if(!r)return EFI_INVALID_PARAMETER;
 memset(r,0,sizeof(*r));r->allocation_status=r->call_status=r->free_status=EFI_NOT_READY;
 EFI_STATUS s=spx_scm_primary_gpt(bs,scm,&r->gpt);r->status=s;
 if(s!=EFI_SUCCESS)return s;
 if(!r->gpt.fsg_found){r->status=EFI_NOT_FOUND;return r->status;}
 if(r->gpt.fsg_blocks>sizeof(r->data)/4096){r->status=EFI_BUFFER_TOO_SMALL;return r->status;}
 EFI_GUID guid={0x9c1eb71f,0xdd6c,0x4ed5,{0x9f,0x6a,0x5c,0xc0,0xca,0x78,0x9f,0x16}};
 struct spx_shm_prefix *bridge=NULL;s=bs->LocateProtocol(&guid,NULL,(VOID**)&bridge);r->allocation_status=s;
 if(s!=EFI_SUCCESS){r->status=EFI_ERROR(s)?s:EFI_DEVICE_ERROR;return r->status;}
 if(!bridge || bridge->Revision!=0x10001ULL || !bridge->Allocate || !bridge->Free){r->status=EFI_UNSUPPORTED;return r->status;}
 UINT64 n=8192;VOID *owned=bridge->Allocate(bridge,&n,SPX_SHM_BOOT);r->allocation_status=owned?EFI_SUCCESS:EFI_OUT_OF_RESOURCES;
 if(!owned){r->status=EFI_OUT_OF_RESOURCES;return r->status;}
 if(n!=8192 || !(UINTN)owned || (UINTN)owned%4096 || (UINTN)owned>0xffffe000){r->free_status=bridge->Free(bridge,owned);r->status=EFI_INVALID_PARAMETER;return r->status;}
 static struct spx_gpt_report transfer;memset(&transfer,0,sizeof(transfer));
 for(UINT64 i=0;i<r->gpt.fsg_blocks;i++){
  s=spx_read_metadata_block(scm,owned,&r->gpt.info.geometry,r->gpt.fsg_first+i,r->data+i*4096,&transfer);
  r->reads=transfer.reads;r->last_lba=transfer.last_lba;r->call_status=transfer.call_status;
  r->returned_size=transfer.returned_size;memcpy(r->results,transfer.results,sizeof(r->results));
  if(s!=EFI_SUCCESS)break;
  r->copied_blocks++;r->bytes+=4096;
 }
 memset(owned,0,8192);WriteBackInvalidateDataCacheRange(owned,8192);r->free_status=bridge->Free(bridge,owned);
 if(s==EFI_SUCCESS && r->free_status!=EFI_SUCCESS)s=EFI_ERROR(r->free_status)?r->free_status:EFI_DEVICE_ERROR;
 r->status=s;r->valid=(s==EFI_SUCCESS && r->copied_blocks==r->gpt.fsg_blocks);
 return s;
}
