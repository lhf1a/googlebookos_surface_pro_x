/* SPDX-License-Identifier: BSD-3-Clause */
#include <efi.h>
#include <efilib.h>
#include "surface-scm-getinfo-efi.h"
#include "primary-gpt.h"
#include "fsg-export.h"
#include "../fsg-record.h"
EFI_STATUS spx_scm_fsg_export(EFI_BOOT_SERVICES *,QCOM_SCM_PROTOCOL *,struct spx_fsg_report *);
static UINT8 record[SPX_FSG_HEADER+SPX_FSG_CAPACITY];
static struct spx_fsg_report report;
static EFI_STATUS read_cache(EFI_FILE_HANDLE f,const char *uuid) {
 UINTN n=sizeof(record); EFI_STATUS s=f->SetPosition(f,0);
 if(s==EFI_SUCCESS)s=f->Read(f,&n,record);
 return s==EFI_SUCCESS && !fsg_record_valid(record,n,uuid)?EFI_COMPROMISED_DATA:s;
}
EFI_STATUS spx_lte_fsg_prepare(EFI_HANDLE image,EFI_SYSTEM_TABLE *st,const char *uuid) {
 EFI_LOADED_IMAGE *loaded=NULL;EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs=NULL;
 EFI_FILE_HANDLE root=NULL,dir=NULL,file=NULL;EFI_STATUS s;UINTN n;
 EFI_GUID lg=EFI_LOADED_IMAGE_PROTOCOL_GUID,fg=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
 EFI_GUID sg={0x77ed108d,0x8524,0x4b8b,{0x9d,0x2e,0x34,0x98,0x7a,0xec,0xb9,0xc1}};
 QCOM_SCM_PROTOCOL *scm=NULL;CHAR16 path[48]={0};
 if(!uuid || strlen(uuid)!=36)return EFI_NOT_FOUND;
 for(unsigned i=0;i<36;i++)path[i]=(CHAR16)uuid[i];
 path[36]='.';path[37]='b';path[38]='i';path[39]='n';
 s=st->BootServices->HandleProtocol(image,&lg,(VOID**)&loaded);if(EFI_ERROR(s))return s;
 s=st->BootServices->HandleProtocol(loaded->DeviceHandle,&fg,(VOID**)&fs);if(EFI_ERROR(s))return s;
 s=fs->OpenVolume(fs,&root);if(EFI_ERROR(s))return s;
 s=root->Open(root,&dir,L"\\EFI\\SPX\\LTE",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,EFI_FILE_DIRECTORY);
 root->Close(root);if(EFI_ERROR(s))return s;
 s=dir->Open(dir,&file,path,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE,0);
 dir->Close(dir);if(EFI_ERROR(s))return s;
 s=read_cache(file,uuid);
 if(s==EFI_SUCCESS)goto done;
 /* Invalid/missing cache triggers collection, never a guessed-value early exit. */
 memset(record,0,SPX_FSG_HEADER);n=SPX_FSG_HEADER;
 s=file->SetPosition(file,0);if(EFI_ERROR(s))goto done;
 s=file->Write(file,&n,record);if(EFI_ERROR(s))goto done;
 if(n!=SPX_FSG_HEADER){s=EFI_DEVICE_ERROR;goto done;}
 s=file->Flush(file);if(EFI_ERROR(s))goto done;
 s=st->BootServices->LocateProtocol(&sg,NULL,(VOID**)&scm);
 if(s==EFI_SUCCESS)s=spx_scm_fsg_export(st->BootServices,scm,&report);
 if(s!=EFI_SUCCESS)Print(L"SPX LTE: FSG status=%r reads=%u bytes=%u service=%lx/%lx.\n",s,report.reads,report.bytes,report.results[0],report.results[1]);
 if(s!=EFI_SUCCESS || !report.valid)goto done;
 memcpy(record,"SPXFSG02",8);fsg_put32(record+8,report.bytes);
 memcpy(record+32,uuid,36);memcpy(record+SPX_FSG_HEADER,report.data,report.bytes);
 fsg_put32(record+16,fsg_crc(report.data,report.bytes,0));
 fsg_put32(record+72,report.reads);fsg_put32(record+76,report.copied_blocks);
 spx_storage_put64(record+80,report.gpt.fsg_first);
 spx_storage_put64(record+88,report.gpt.fsg_blocks);
 fsg_put32(record+20,fsg_crc(record,128,1));
 n=SPX_FSG_HEADER+report.bytes;s=file->SetPosition(file,0);
 if(s==EFI_SUCCESS)s=file->Write(file,&n,record);
 if(s==EFI_SUCCESS && n!=SPX_FSG_HEADER+report.bytes)s=EFI_DEVICE_ERROR;
 if(s==EFI_SUCCESS)s=file->Flush(file);
 if(s==EFI_SUCCESS)s=read_cache(file,uuid);
done:
 {EFI_STATUS closed=file->Close(file);if(s==EFI_SUCCESS)s=closed;}
 memset(record,0,sizeof(record));memset(&report,0,sizeof(report));return s;
}
