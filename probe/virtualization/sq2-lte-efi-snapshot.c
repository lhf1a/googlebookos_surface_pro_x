// SPDX-License-Identifier: BSD-3-Clause
/* One-shot read-only EFS export before EL2. Never calls BlockIo WriteBlocks,
 * reset, SCM writes or variable services. Chain the byte-preserved launcher
 * whether capture succeeds or fails. ARM is consumed before any capture.
 */
#include <efi.h>
#include <efilib.h>
#include <efigpt.h>

static EFI_FILE_HANDLE report;
static CHAR16 line[512];
static void log_line(void) {
    CHAR8 ascii[512]; UINTN n=0;
    while(line[n] && n<sizeof(ascii)-2) {
        ascii[n]=line[n]>=32 && line[n]<127 ? (CHAR8)line[n] : '?'; n++;
    }
    ascii[n++]='\n';
    if(report) uefi_call_wrapper(report->Write,3,report,&n,ascii);
}
#define LOG(fmt, ...) do { SPrint(line,sizeof(line),fmt,##__VA_ARGS__); log_line(); } while(0)

static EFI_FILE_HANDLE open_file(EFI_FILE_HANDLE dir, CHAR16 *name, UINT64 mode) {
    EFI_FILE_HANDLE f=NULL;
    if(EFI_ERROR(uefi_call_wrapper(dir->Open,5,dir,&f,name,mode,0))) return NULL;
    return f;
}
static EFI_STATUS save(EFI_FILE_HANDLE dir, CHAR16 *name, VOID *buf, UINTN bytes) {
    EFI_FILE_HANDLE f=open_file(dir,name,EFI_FILE_MODE_READ);
    if(f) { uefi_call_wrapper(f->Close,1,f); return EFI_ALREADY_STARTED; }
    f=open_file(dir,name,EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE);
    if(!f) return EFI_DEVICE_ERROR;
    UINTN actual=bytes;
    EFI_STATUS ret=uefi_call_wrapper(f->Write,3,f,&actual,buf);
    if(!EFI_ERROR(ret) && actual!=bytes) ret=EFI_DEVICE_ERROR;
    if(!EFI_ERROR(ret)) ret=uefi_call_wrapper(f->Flush,1,f);
    uefi_call_wrapper(f->Close,1,f);
    return ret;
}
static VOID *read_blocks(EFI_BLOCK_IO *io, EFI_LBA lba, UINTN bytes) {
    EFI_PHYSICAL_ADDRESS addr=0;
    UINTN pages=(bytes+4095)/4096;
    if(!bytes || bytes%io->Media->BlockSize || io->Media->IoAlign>4096 ||
       lba>io->Media->LastBlock || bytes/io->Media->BlockSize-1>io->Media->LastBlock-lba)
        return NULL;
    if(EFI_ERROR(uefi_call_wrapper(BS->AllocatePages,4,AllocateAnyPages,EfiLoaderData,pages,&addr)))
        return NULL;
    EFI_STATUS ret=uefi_call_wrapper(io->ReadBlocks,5,io,io->Media->MediaId,lba,bytes,(VOID*)addr);
    if(EFI_ERROR(ret)) {
        uefi_call_wrapper(BS->FreePages,2,addr,pages);
        LOG(L"ReadBlocks lba=%lu bytes=%lu status=%r",lba,bytes,ret);
        return NULL;
    }
    return (VOID*)addr;
}
static void release(VOID *buf, UINTN bytes) {
    if(buf) uefi_call_wrapper(BS->FreePages,2,(EFI_PHYSICAL_ADDRESS)buf,(bytes+4095)/4096);
}
static BOOLEAN efs_name(CHAR16 *name) {
    static CHAR16 *names[]={L"modem_fs1",L"modem_fs2",L"modem_fsg",L"modem_fsc",
                           L"modemst1",L"modemst2",L"fsg",L"fsc",
                           L"modem_tuning",L"modem_tunning",L"modem_study",L"modem_tng"};
    for(UINTN i=0;i<sizeof(names)/sizeof(names[0]);i++) if(!StrCmp(name,names[i])) return TRUE;
    return FALSE;
}
static void capture_disk(EFI_FILE_HANDLE dir, EFI_BLOCK_IO *io, UINTN handle) {
    UINTN block=io->Media->BlockSize;
    VOID *header=read_blocks(io,1,block);
    if(!header) return;
    EFI_PARTITION_TABLE_HEADER h;
    CopyMem(&h,header,sizeof(h));
    if(CompareMem(header,"EFI PART",8) || h.Header.HeaderSize<sizeof(h) ||
       h.Header.HeaderSize>block || h.MyLBA!=1 || h.NumberOfPartitionEntries>1024 ||
       !h.NumberOfPartitionEntries || h.SizeOfPartitionEntry<sizeof(EFI_PARTITION_ENTRY) ||
       h.SizeOfPartitionEntry>1024 || h.SizeOfPartitionEntry%8) goto done;
    UINT32 crc,expected=((EFI_PARTITION_TABLE_HEADER*)header)->Header.CRC32;
    ((EFI_PARTITION_TABLE_HEADER*)header)->Header.CRC32=0;
    if(EFI_ERROR(uefi_call_wrapper(BS->CalculateCrc32,3,header,h.Header.HeaderSize,&crc)) || crc!=expected)
        goto done;
    UINTN size=(UINTN)h.NumberOfPartitionEntries*h.SizeOfPartitionEntry;
    UINTN readsize=(size+block-1)/block*block;
    UINT8 *table=read_blocks(io,h.PartitionEntryLBA,readsize);
    if(!table) goto done;
    if(EFI_ERROR(uefi_call_wrapper(BS->CalculateCrc32,3,table,size,&crc)) ||
       crc!=h.PartitionEntryArrayCRC32) { release(table,readsize); goto done; }
    LOG(L"GPT handle=%lu entries=%u",handle,h.NumberOfPartitionEntries);
    for(UINTN i=0;i<h.NumberOfPartitionEntries;i++) {
        EFI_PARTITION_ENTRY entry;
        CopyMem(&entry,table+i*h.SizeOfPartitionEntry,sizeof(entry));
        entry.PartitionName[35]=0;
        if(!entry.StartingLBA && !entry.EndingLBA) continue;
        LOG(L"PART handle=%lu index=%lu name=%s start=%lu end=%lu",handle,i,
            entry.PartitionName,entry.StartingLBA,entry.EndingLBA);
        if(!efs_name(entry.PartitionName) || entry.EndingLBA<entry.StartingLBA ||
           entry.StartingLBA<h.FirstUsableLBA || entry.EndingLBA>h.LastUsableLBA ||
           entry.EndingLBA-entry.StartingLBA+1>8*1024*1024/block) continue;
        UINTN bytes=(entry.EndingLBA-entry.StartingLBA+1)*block;
        VOID *buf=read_blocks(io,entry.StartingLBA,bytes);
        if(!buf) continue;
        CHAR16 name[80];
        SPrint(name,sizeof(name),L"%s-h%lu.bin",entry.PartitionName,handle);
        EFI_STATUS ret=save(dir,name,buf,bytes);
        LOG(L"EXPORT file=%s bytes=%lu status=%r",name,bytes,ret);
        release(buf,bytes);
    }
    release(table,readsize);
done:
    release(header,block);
}
static void capture(EFI_FILE_HANDLE root) {
    EFI_FILE_HANDLE dir=open_file(root,L"\\EFI\\SQ2LTE",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE);
    if(!dir) return;
    EFI_FILE_HANDLE arm=open_file(dir,L"ARM",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE);
    if(!arm) { uefi_call_wrapper(dir->Close,1,dir); return; }
    EFI_STATUS ret=uefi_call_wrapper(arm->Delete,1,arm);
    if(EFI_ERROR(ret)) { uefi_call_wrapper(dir->Close,1,dir); return; }
    report=open_file(dir,L"capture.txt",EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE);
    if(!report) { uefi_call_wrapper(dir->Close,1,dir); return; }
    EFI_GUID guid=BLOCK_IO_PROTOCOL;
    EFI_HANDLE *handles=NULL; UINTN count=0;
    ret=uefi_call_wrapper(BS->LocateHandleBuffer,5,ByProtocol,&guid,NULL,&count,&handles);
    LOG(L"BLOCK_IO handles=%lu status=%r",count,ret);
    if(!EFI_ERROR(ret)) for(UINTN i=0;i<count;i++) {
        EFI_BLOCK_IO *io=NULL;
        ret=uefi_call_wrapper(BS->HandleProtocol,3,handles[i],&guid,(VOID**)&io);
        if(EFI_ERROR(ret) || !io || !io->Media) continue;
        EFI_BLOCK_IO_MEDIA *m=io->Media;
        LOG(L"MEDIA handle=%lu present=%u logical=%u readonly=%u block=%u last=%lu align=%u",
            i,m->MediaPresent,m->LogicalPartition,m->ReadOnly,m->BlockSize,m->LastBlock,m->IoAlign);
        /* NOR candidates only. Never read partition tables or data from NVMe. */
        if(!m->MediaPresent || m->LogicalPartition || (m->BlockSize!=512 && m->BlockSize!=4096) ||
           m->LastBlock>=1024*1024*1024ULL/m->BlockSize) continue;
        capture_disk(dir,io,i);
    }
    if(handles) FreePool(handles);
    LOG(L"CAPTURE_COMPLETE; chaining original launcher");
    uefi_call_wrapper(report->Flush,1,report);
    uefi_call_wrapper(report->Close,1,report); report=NULL;
    uefi_call_wrapper(dir->Close,1,dir);
}
EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st) {
    InitializeLib(image,st);
    EFI_GUID loaded_guid=LOADED_IMAGE_PROTOCOL,fs_guid=SIMPLE_FILE_SYSTEM_PROTOCOL;
    EFI_LOADED_IMAGE *loaded=NULL;
    EFI_FILE_IO_INTERFACE *fs=NULL; EFI_FILE_HANDLE root=NULL;
    EFI_STATUS ret=uefi_call_wrapper(BS->HandleProtocol,3,image,&loaded_guid,(VOID**)&loaded);
    if(EFI_ERROR(ret)) return ret;
    ret=uefi_call_wrapper(BS->HandleProtocol,3,loaded->DeviceHandle,&fs_guid,(VOID**)&fs);
    if(!EFI_ERROR(ret)) ret=uefi_call_wrapper(fs->OpenVolume,2,fs,&root);
    if(!EFI_ERROR(ret) && root) { capture(root); uefi_call_wrapper(root->Close,1,root); }
    EFI_DEVICE_PATH *path=FileDevicePath(loaded->DeviceHandle,L"\\EFI\\SQ2Default\\el2launch.before-lte.efi");
    if(!path) return EFI_OUT_OF_RESOURCES;
    EFI_HANDLE next=NULL;
    ret=uefi_call_wrapper(BS->LoadImage,6,FALSE,image,path,NULL,0,&next);
    FreePool(path);
    if(EFI_ERROR(ret)) return ret;
    return uefi_call_wrapper(BS->StartImage,3,next,NULL,NULL);
}
