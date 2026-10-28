#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <elf.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
int quest2_composition_main();
extern "C" int quest2_vulkan_window_main(int, char **);
extern "C" int quest2_composition_c_entry() { return quest2_composition_main(); }
static uintptr_t module(const char *name) {
    FILE *maps=fopen("/proc/self/maps","re"); if(!maps) return 0;
    char line[4096],path[2048],filename[2048]={0}; unsigned long long low,high,offset;
    struct Span {uintptr_t low,high,offset;} spans[128]; unsigned count=0,matches=0;uintptr_t base=0;
    while(fgets(line,sizeof(line),maps)) {
        if(sscanf(line,"%llx-%llx %*s %llx %*s %*s %2047s",&low,&high,&offset,path)!=4) continue;
        const char *part=strrchr(path,'/');
        if(part && !strcmp(part+1,name)) {
            if(count==128){fclose(maps);return 0;}
            spans[count++]={low,high,offset};snprintf(filename,sizeof(filename),"%s",path);
        }
    }
    fclose(maps);
    FILE *elf=fopen(filename,"re"); if(!elf)return 0;
    Elf64_Ehdr header{};Elf64_Phdr ph[64];
    bool valid=fread(&header,sizeof(header),1,elf)==1 && !memcmp(header.e_ident,ELFMAG,SELFMAG) &&
        header.e_phnum<=64 && header.e_phentsize==sizeof(Elf64_Phdr) &&
        !fseek(elf,header.e_phoff,SEEK_SET) && fread(ph,sizeof(Elf64_Phdr),header.e_phnum,elf)==header.e_phnum;
    fclose(elf);if(!valid)return 0;
    // Small 16K-aligned libraries map file page zero several times. A load
    // instance must satisfy every PT_LOAD at one common bias, not one map row.
    for(unsigned i=0;i<count;i++)if(!spans[i].offset) {
        bool complete=true;
        for(unsigned j=0;j<header.e_phnum;j++)if(ph[j].p_type==PT_LOAD&&ph[j].p_filesz) {
            uintptr_t address=spans[i].low+ph[j].p_vaddr; bool found=false;
            for(unsigned k=0;k<count;k++) if(address>=spans[k].low && address<spans[k].high &&
                spans[k].offset+address-spans[k].low==ph[j].p_offset)found=true;
            complete=complete&&found;
        }
        if(complete){++matches;base=spans[i].low;}
    }
    printf("COMBINED_MODULE name=%s instances=%u\n",name,matches);
    return matches==1?base:0;
}
extern "C" int quest2_combined_audit() {
    uintptr_t gsl=module("libgsl.so"),mapper=module("libqti-mapper3-bridge.so"),
        adapter=module("libquest2-v77-ahb.so"),ion=module("libquest2-ion-dmaheap.so"),
        donor=module("vulkan-v77-bridge.so"),platform=module("libnativewindow.so");
    if(!gsl||!mapper||!adapter||!ion||!donor||!platform) return 1;
    int mem=open("/proc/self/mem",O_RDONLY|O_CLOEXEC); if(mem<0) return 1;
    struct Slot {uintptr_t source; size_t offset; uintptr_t target; const char *name;};
    const char *revision=getenv("SQ2_COMBINED_REVISION");
    if(revision&&(!strcmp(revision,"4")||!strcmp(revision,"5"))&&(!module("libq2v77glnext.so")||!module("libq2v77qgl.so")))return 1;
    size_t resolver_slot=revision&&(!strcmp(revision,"3")||!strcmp(revision,"4"))?0x1aab0:0x1a660;
    if(revision&&!strcmp(revision,"5"))resolver_slot=0x1a9a0;
    const Slot slots[]={{gsl,0x213f60,ion,"ion_open"},{gsl,0x213f68,ion,"ion_close"},
        {gsl,0x213f90,ion,"ion_alloc_fd"},{mapper,resolver_slot,adapter,"sq2_v77_original_handle"},
        {donor,0x2e2be0,adapter,"SQ2NativeBuffer_getNativeHandle"},
        {donor,0x2e2bf0,adapter,"SQ2NativeBuffer_acquire"},
        {donor,0x2e3150,adapter,"SQ2NativeBuffer_release"},
        {donor,0x2e3160,adapter,"SQ2NativeBuffer_allocate"},
        {donor,0x2e2be8,platform,"AHardwareBuffer_describe"},
        {adapter,0x11f70,platform,"AHardwareBuffer_acquire"},
        {adapter,0x11f78,platform,"AHardwareBuffer_getNativeHandle"},
        {adapter,0x11f88,platform,"AHardwareBuffer_describe"},
        {adapter,0x11fa0,platform,"AHardwareBuffer_release"}};
    bool ok=true;
    for(const auto &slot:slots) {
        uintptr_t target=0;Dl_info origin{};
        bool valid=pread(mem,&target,8,slot.source+slot.offset)==8 && target &&
            dladdr(reinterpret_cast<void*>(target),&origin) &&
            reinterpret_cast<uintptr_t>(origin.dli_fbase)==slot.target &&
            origin.dli_sname && !strcmp(origin.dli_sname,slot.name);
        printf("COMBINED_BIND name=%s module=%s symbol=%s status=%s\n",slot.name,
            origin.dli_fname?origin.dli_fname:"unavailable",origin.dli_sname?origin.dli_sname:"unavailable",valid?"PASS":"FAIL");
        ok=ok&&valid;
    }
    close(mem);return ok?0:1;
}
int main() {
    const char *order=std::getenv("SQ2_COMBINED_ORDER");
    if(!order || (std::strcmp(order,"egl-first") && std::strcmp(order,"vulkan-first"))) return 2;
    // Diagnostic controls cannot turn a nested partial probe into a pass.
    if(std::getenv("SQ2_COMPOSITION_FD_DIAGNOSTIC") ||
       std::getenv("SQ2_COMPOSITION_FD_CPU_CONTROL") || std::getenv("SQ2_COMPOSITION_FD_IMPORT_CONTROL")) return 2;
    int result;
    if(!std::strcmp(order,"egl-first")) result=quest2_composition_main();
    else { char name[]="quest2-combined"; char *args[]={name,nullptr}; result=quest2_vulkan_window_main(1,args); }
    std::printf("COMBINED_RESULT order=%s result=%d status=%s\n",order,result,result?"FAIL":"PASS");
    return result;
}
