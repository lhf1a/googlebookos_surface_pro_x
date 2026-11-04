/* Offline-built EFI router. No firmware loading or device writes here.
 * Unknown/ambiguous SMBIOS processor identity fails closed, never guesses SQ2. */
#include <efi.h>
#include <efilib.h>
#ifndef FALLBACK
#define FALLBACK 0
#endif
static UINT64 le(const UINT8 *p, UINTN n) {
    UINT64 v=0; UINTN i; for(i=0;i<n;i++) v|=(UINT64)p[i]<<(8*i); return v;
}
static int sumok(const UINT8 *p, UINTN n) {
    UINT8 s=0; while(n--) s+=*p++; return s==0;
}
static int begins(const UINT8 *p, UINTN n, const char *s) {
    UINTN i=0; while(s[i]) { if(i>=n || p[i]!=(UINT8)s[i]) return 0; i++; } return 1;
}
static int identify(const UINT8 *p, UINTN bytes) {
    UINTN off=0, records=0; int found=0;
    while(off+4<=bytes && records++<8192) {
        UINTN len=p[off+1], end, s, index=1; int model=0;
        if(len<4 || off+len>bytes) return 0;
        end=off+len;
        while(end+1<bytes && (p[end] || p[end+1])) end++;
        if(end+1>=bytes) return 0;
        if(p[off]==4 && len>16 && p[off+16]) {
            s=off+len;
            while(s<end && index<p[off+16]) { while(s<end && p[s]) s++; s++; index++; }
            if(index!=p[off+16] || s>=end) return 0;
            if(begins(p+s,end-s,"Microsoft SQ1") && (p[s+13]==0 || p[s+13]==' ')) model=1;
            if(begins(p+s,end-s,"Microsoft SQ2") && (p[s+13]==0 || p[s+13]==' ')) model=2;
            if(!model || (found && found!=model)) return 0;
            found=model;
        }
        if(p[off]==127) break;
        off=end+2;
    }
    return found;
}
EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st) {
    EFI_GUID g2={0xeb9d2d31,0x2d88,0x11d3,{0x9a,0x16,0,0x90,0x27,0x3f,0xc1,0x4d}};
    EFI_GUID g3={0xf2fd1544,0x9794,0x4a2c,{0x99,0x2e,0xe5,0xbb,0xcf,0x20,0xe3,0x94}};
    UINTN i; int model=0; EFI_LOADED_IMAGE *loaded; EFI_HANDLE child;
    EFI_DEVICE_PATH *path; EFI_STATUS ret; CHAR16 *name;
    InitializeLib(image,st);
    for(i=0;i<st->NumberOfTableEntries;i++) {
        EFI_CONFIGURATION_TABLE *t=&st->ConfigurationTable[i]; const UINT8 *p=t->VendorTable;
        UINT64 addr=0; UINTN size=0; int m;
        if(!CompareGuid(&t->VendorGuid,&g3) && begins(p,5,"_SM3_") && p[6]>=24 && p[6]<=32 && sumok(p,p[6])) {
            size=le(p+12,4); addr=le(p+16,8);
        } else if(!CompareGuid(&t->VendorGuid,&g2) && begins(p,4,"_SM_") && p[5]>=31 && p[5]<=32 && sumok(p,p[5])) {
            size=le(p+22,2); addr=le(p+24,4);
        }
        if(!addr || size<4 || size>1048576) continue;
        m=identify((const UINT8 *)(UINTN)addr,size);
        if(!m || (model && model!=m)) { Print(L"SPX: ambiguous processor identity; stopped.\n"); return EFI_UNSUPPORTED; }
        model=m;
    }
    if(!model) { Print(L"SPX: SQ1/SQ2 SMBIOS identity unavailable; stopped.\n"); return EFI_UNSUPPORTED; }
    name=model==1 ? (FALLBACK?L"\\EFI\\SQ1Fallback\\el2launch.efi":L"\\EFI\\SQ1Default\\el2launch.efi")
                  : (FALLBACK?L"\\EFI\\SQ2Fallback\\el2launch.efi":L"\\EFI\\SQ2Default\\el2launch.efi");
    ret=uefi_call_wrapper(st->BootServices->HandleProtocol,3,image,&LoadedImageProtocol,(VOID **)&loaded);
    if(EFI_ERROR(ret)) return ret;
    path=FileDevicePath(loaded->DeviceHandle,name); if(!path) return EFI_OUT_OF_RESOURCES;
    ret=uefi_call_wrapper(st->BootServices->LoadImage,6,FALSE,image,path,NULL,0,&child); FreePool(path);
    if(EFI_ERROR(ret)) return ret;
    return uefi_call_wrapper(st->BootServices->StartImage,3,child,NULL,NULL);
}
