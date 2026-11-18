/* SPDX-License-Identifier: BSD-2-Clause
 * Loaded between the unchanged EL1/EL2 launcher and Linux's EFI stub.
 * Publish a fresh boot-instance token ONLY on our own loaded filesystem, then
 * pass it via Linux LoadOptions. No serial, fixed UUID or machine admission.
 */
#include <efi.h>
#include <efilib.h>

typedef struct spx_rng SPX_RNG;
struct spx_rng {
 EFI_STATUS (EFIAPI *GetInfo)(SPX_RNG *,UINTN *,EFI_GUID *);
 EFI_STATUS (EFIAPI *GetRNG)(SPX_RNG *,EFI_GUID *,UINTN,UINT8 *);
};
static EFI_GUID rng_guid={0x3152bca5,0xeade,0x433d,{0x86,0x2e,0xc0,0x1c,0xdc,0x29,0x1f,0x44}};
static CHAR16 marker_name[]=L"\\SPX-BOOT-SOURCE.txt";
static CHAR16 option_name[]=L" spx.boot_token=";
static BOOLEAN has_source_option(const CHAR16 *text,UINTN chars)
{
 static const CHAR16 key[]=L"spx.boot_token=";
 UINTN i,j,n=sizeof(key)/sizeof(key[0])-1;
 for(i=0;i+n<=chars;i++) {
  for(j=0;j<n && text[i+j]==key[j];j++);
  if(j==n)return TRUE;
 }
 return FALSE;
}

static EFI_STATUS fresh_token(UINT8 raw[32])
{
 SPX_RNG *rng=NULL;EFI_STATUS r;EFI_TIME time;UINT64 count,counter;
 r=uefi_call_wrapper(BS->LocateProtocol,3,&rng_guid,NULL,(VOID **)&rng);
 if(!EFI_ERROR(r)) {
  r=uefi_call_wrapper(rng->GetRNG,4,rng,NULL,32,raw);
  if(!EFI_ERROR(r))return r;
 }
 /* Token is a boot-instance locator, not an authentication secret. Older
  * firmware without RNG uses the EFI monotonic counter, RTC and CPU counter.
  * Duplicate matches still fail closed in Linux; never pick the first disk.
  */
 SetMem(&time,sizeof(time),0);
 r=uefi_call_wrapper(RT->GetTime,2,&time,NULL);if(EFI_ERROR(r))return r;
 r=uefi_call_wrapper(BS->GetNextMonotonicCount,1,&count);if(EFI_ERROR(r))return r;
 __asm__ volatile("mrs %0, cntpct_el0":"=r"(counter));
 SetMem(raw,32,0);CopyMem(raw,&time,sizeof(time));CopyMem(raw+16,&count,8);CopyMem(raw+24,&counter,8);
 return EFI_SUCCESS;
}

static EFI_STATUS write_marker(EFI_HANDLE device,const CHAR8 text[65])
{
 EFI_FILE_HANDLE root,file=NULL;EFI_FILE_INFO *info;EFI_STATUS r;UINTN n;CHAR8 check[66];
 root=LibOpenRoot(device);if(!root)return EFI_NOT_FOUND;
 r=uefi_call_wrapper(root->Open,5,root,&file,marker_name,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);
 if(EFI_ERROR(r))goto done;
 info=LibFileInfo(file);if(!info){r=EFI_OUT_OF_RESOURCES;goto close;}
 info->FileSize=65;
 r=uefi_call_wrapper(file->SetInfo,4,file,&GenericFileInfo,info->Size,info);FreePool(info);
 if(EFI_ERROR(r))goto close;
 r=uefi_call_wrapper(file->SetPosition,2,file,0);if(EFI_ERROR(r))goto close;
 n=65;r=uefi_call_wrapper(file->Write,3,file,&n,(VOID *)text);
 if(EFI_ERROR(r)||n!=65){r=EFI_DEVICE_ERROR;goto close;}
 r=uefi_call_wrapper(file->Flush,1,file);if(EFI_ERROR(r))goto close;
 r=uefi_call_wrapper(file->SetPosition,2,file,0);if(EFI_ERROR(r))goto close;
 n=sizeof(check);r=uefi_call_wrapper(file->Read,3,file,&n,check);
 if(EFI_ERROR(r)||n!=65||CompareMem(check,text,65))r=EFI_DEVICE_ERROR;
close:
 uefi_call_wrapper(file->Close,1,file);
done:
 uefi_call_wrapper(root->Close,1,root);return r;
}

static CHAR16 *kernel_path(EFI_LOADED_IMAGE *loaded)
{
 EFI_DEVICE_PATH *p=loaded->FilePath;CHAR16 *result;UINTN used=0,last=0;
 result=AllocateZeroPool(4096);if(!result)return NULL;
 while(!IsDevicePathEnd(p)) {
  UINTN len=DevicePathNodeLength(p);
  if(len<sizeof(EFI_DEVICE_PATH)){FreePool(result);return NULL;}
  if(DevicePathType(p)==MEDIA_DEVICE_PATH && p->SubType==MEDIA_FILEPATH_DP) {
   FILEPATH_DEVICE_PATH *f=(FILEPATH_DEVICE_PATH *)p;
   UINTN chars=(len-SIZE_OF_FILEPATH_DEVICE_PATH)/sizeof(CHAR16),i;
   for(i=0;i<chars && f->PathName[i];i++) {
    if(used>=2038){FreePool(result);return NULL;}
    result[used++]=f->PathName[i];if(f->PathName[i]==L'\\')last=used;
   }
  }
  p=NextDevicePathNode(p);
 }
 if(!last){FreePool(result);return NULL;}
 StrCpy(result+last,L"Image");return result;
}

EFI_STATUS efi_main(EFI_HANDLE image,EFI_SYSTEM_TABLE *st)
{
 EFI_LOADED_IMAGE *self,*child_info;EFI_HANDLE child;EFI_DEVICE_PATH *path=NULL;
 EFI_STATUS r;UINT8 raw[32];CHAR8 token[65];CHAR16 *options=NULL,*name=NULL;
 CHAR16 *old;UINTN chars,i;static const CHAR8 hex[]="0123456789abcdef";
 InitializeLib(image,st);
 r=uefi_call_wrapper(BS->HandleProtocol,3,image,&LoadedImageProtocol,(VOID **)&self);
 if(EFI_ERROR(r))return r;
 if(!self->DeviceHandle || !self->LoadOptions || self->LoadOptionsSize<2 || self->LoadOptionsSize%2)return EFI_INVALID_PARAMETER;
 old=self->LoadOptions;chars=self->LoadOptionsSize/2;
 for(i=0;i<chars && old[i];i++);
 if(i==chars || has_source_option(old,i))return EFI_INVALID_PARAMETER;
 name=kernel_path(self);if(!name)return EFI_INVALID_PARAMETER;
 r=fresh_token(raw);if(EFI_ERROR(r))goto done;
 for(i=0;i<32;i++){token[2*i]=hex[raw[i]>>4];token[2*i+1]=hex[raw[i]&15];}
 token[64]='\n';r=write_marker(self->DeviceHandle,token);if(EFI_ERROR(r))goto done;
 options=AllocateZeroPool(self->LoadOptionsSize+sizeof(option_name)+128);
 if(!options){r=EFI_OUT_OF_RESOURCES;goto done;}
 StrCpy(options,old);StrCat(options,option_name);chars=StrLen(options);
 for(i=0;i<64;i++)options[chars+i]=(CHAR16)token[i];
 options[chars+64]=0;
 path=FileDevicePath(self->DeviceHandle,name);if(!path){r=EFI_OUT_OF_RESOURCES;goto done;}
 r=uefi_call_wrapper(BS->LoadImage,6,FALSE,image,path,NULL,0,&child);if(EFI_ERROR(r))goto done;
 r=uefi_call_wrapper(BS->HandleProtocol,3,child,&LoadedImageProtocol,(VOID **)&child_info);
 if(EFI_ERROR(r)){uefi_call_wrapper(BS->UnloadImage,1,child);goto done;}
 child_info->LoadOptions=options;child_info->LoadOptionsSize=(StrLen(options)+1)*sizeof(CHAR16);
 Print(L"SPX boot source: own ESP recorded; starting %s\n",name);
 r=uefi_call_wrapper(BS->StartImage,3,child,NULL,NULL);
done:
 if(EFI_ERROR(r))Print(L"SPX_BOOT_SOURCE_FAILED: %r; no alternate disk selected.\n",r);
 if(path)FreePool(path);
 if(name)FreePool(name);
 if(options)FreePool(options);
 return r;
}
