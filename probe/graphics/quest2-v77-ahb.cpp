/* Private, fixed-version v77 AHB/native-handle adaptation. Never expose these
 * QTI views to Android, IPC, or the real minigbm mapper. See the lifetime audit.
 * Supported initially: single-layer, unprotected, zero-offset linear RGBA8.
 * The donor owns any CPU mapping it creates; view.base deliberately stays 0. */
#include <android/hardware_buffer.h>
#include <hardware/hardware.h>
#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#include "quest2-mapper5.h"
#include "quest2-v77-native.h"

// Native platform entry absent from the public NDK header, present in the
// platform libnativewindow used by the matched system-loader namespace.
extern "C" const native_handle_t *AHardwareBuffer_getNativeHandle(const AHardwareBuffer*);

namespace {
// Matched v77 libgralloccore validate/constructor: 124 bytes, 2 fds, 26 ints.
// The three reserved tail words are zero in that constructor; their semantics
// are not claimed here. The older 108-byte AdrenoTools header is insufficient.
#pragma pack(push,4)
struct View {
    int version, num_fds, num_ints, fd, metadata_fd;
    int magic, flags, width, height, unaligned_width, unaligned_height;
    int format, buffer_type; unsigned layers;
    uint64_t id, usage;
    uint32_t size, offset, metadata_offset;
    uint64_t base, metadata_base, gpu_address;
    uint32_t reserved[3], original_format;
};
#pragma pack(pop)
static_assert(sizeof(View)==124 && offsetof(View,flags)==24 &&
    offsetof(View,format)==44 && offsetof(View,size)==72 && offsetof(View,base)==84 &&
    offsetof(View,original_format)==120);
struct Entry {
    const AHardwareBuffer *ahb=nullptr;
    const native_handle_t *original=nullptr;
    View view={}; unsigned leases=0, owners=0;
};
struct NativeEntry {
    AIMapper *mapper=nullptr;
    buffer_handle_t imported=nullptr;
    View view={};
};
pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
Entry entries[128]; uint64_t bytes=0;
NativeEntry native_entries[128]; uint64_t native_bytes=0;
constexpr uint64_t limit_bytes=512ull*1024*1024;

[[noreturn]] void invariant(const char *operation,uintptr_t site=0) {
    std::fprintf(stderr,"V77_AHB_INVARIANT operation=%s caller=%zx\n",operation,site);
    // A broken private wrapper contract must stop before passing an invalid
    // CPU address/handle to the GPU. Never fall back to a minigbm raw handle.
    std::abort();
}
uintptr_t caller(void *pc) {
    Dl_info info={};
    if(!dladdr(pc,&info)||!info.dli_fbase||!info.dli_fname)invariant("caller-module");
    const char *name=std::strrchr(info.dli_fname,'/');name=name?name+1:info.dli_fname;
    if(std::strcmp(name,"vulkan-v77-bridge.so") && std::strcmp(name,"vulkan.freedreno.so"))
        invariant("caller-name");
    // Preparation pins the entire donor hash and preserves executable bytes.
    return reinterpret_cast<uintptr_t>(pc)-reinterpret_cast<uintptr_t>(info.dli_fbase);
}
Entry *find(const AHardwareBuffer *ahb) {
    for(auto &entry:entries)if(ahb && entry.ahb==ahb)return &entry;
    return nullptr;
}
Entry remove_if_idle(Entry *entry) {
    Entry dead;
    if(entry && !entry->leases && !entry->owners){
        dead=*entry;bytes-=entry->view.size;*entry={};
    }
    return dead;
}
void dispose(const Entry &dead) {
    if(!dead.ahb)return;
    close(dead.view.fd);
    AHardwareBuffer_release(const_cast<AHardwareBuffer*>(dead.ahb));
}
bool color_metadata(const native_handle_t *native,sq2::BufferInfo &b) {
    if(!sq2::color_info(native,b) || b.format!=AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM ||
       b.fourcc!=0x34324241u || b.sample_bits!=32 || b.offset || b.layers!=1 ||
       (b.usage&AHARDWAREBUFFER_USAGE_PROTECTED_CONTENT) || b.fd_size>UINT32_MAX ||
       b.stride%4 || b.stride/4>INT_MAX || b.plane_size%b.stride ||
       b.plane_size/b.stride>INT_MAX || b.width>b.stride/4 ||
       b.height>b.plane_size/b.stride || b.width>INT_MAX || b.height>INT_MAX)return false;
    return true;
}
bool metadata(const AHardwareBuffer *ahb,const native_handle_t *native,sq2::BufferInfo &b) {
    if(!color_metadata(native,b))return false;
    AHardwareBuffer_Desc desc={};AHardwareBuffer_describe(ahb,&desc);
    return desc.width==b.width && desc.height==b.height && desc.layers==b.layers &&
        desc.format==b.format && desc.stride==b.stride/4 && desc.usage==b.usage;
}
View make_view(const sq2::BufferInfo &b,int fd) {
    View v={};v.version=sizeof(native_handle_t);v.num_fds=2;v.num_ints=26;
    v.fd=fd;v.metadata_fd=-1;v.magic=0x676d736d;v.flags=8;
    v.width=b.stride/4;v.height=b.plane_size/b.stride;
    v.unaligned_width=b.width;v.unaligned_height=b.height;
    v.format=b.format;v.original_format=b.format;
    v.layers=1;v.id=b.id;v.usage=b.usage;v.size=b.fd_size;
    return v;
}
}

// Public Vulkan wrappers acquire a transaction before entering donor code.
// Negative errno distinguishes unsupported metadata from resource exhaustion.
extern "C" int sq2_v77_ahb_prepare(const AHardwareBuffer *ahb) {
    if(!ahb)return -EINVAL;
    AHardwareBuffer_acquire(const_cast<AHardwareBuffer*>(ahb));
    const native_handle_t *native=AHardwareBuffer_getNativeHandle(ahb);
    sq2::BufferInfo b;
    if(!metadata(ahb,native,b)){
        AHardwareBuffer_Desc desc={};AHardwareBuffer_describe(ahb,&desc);
        std::fprintf(stderr,"V77_AHB_REJECT format=%u/%u dimensions=%ux%u/%llux%llu stride=%u/%llu usage=%llx/%llx offset=%llu size=%llu\n",
            desc.format,b.format,desc.width,desc.height,(unsigned long long)b.width,(unsigned long long)b.height,
            desc.stride,(unsigned long long)b.stride,(unsigned long long)desc.usage,(unsigned long long)b.usage,
            (unsigned long long)b.offset,(unsigned long long)b.fd_size);
        AHardwareBuffer_release(const_cast<AHardwareBuffer*>(ahb));return -ENOTSUP;
    }
    int result=0;bool transferred=false;
    pthread_mutex_lock(&mutex);
    Entry *entry=find(ahb);
    if(entry){
        if(entry->view.id!=b.id || entry->leases==UINT_MAX)result=-EOVERFLOW;
        else entry->leases++;
    }else{
        for(auto &candidate:entries)if(!candidate.ahb){entry=&candidate;break;}
        if(!entry || b.fd_size>limit_bytes-bytes-native_bytes)result=-ENOSPC;
        else{
            int fd=fcntl(b.fd,F_DUPFD_CLOEXEC,0);
            if(fd<0)result=-ENOMEM;
            else{
                View v=make_view(b,fd);
                *entry={ahb,native,v,1,0};bytes+=b.fd_size;transferred=true;
            }
        }
    }
    pthread_mutex_unlock(&mutex);
    if(!transferred)AHardwareBuffer_release(const_cast<AHardwareBuffer*>(ahb));
    return result;
}
extern "C" void sq2_v77_ahb_finish(const AHardwareBuffer *ahb) {
    pthread_mutex_lock(&mutex);
    Entry *entry=find(ahb);
    if(!entry || !entry->leases)invariant("finish-without-lease");
    --entry->leases;Entry dead=remove_if_idle(entry);
    pthread_mutex_unlock(&mutex);dispose(dead);
}
extern "C" const native_handle_t *sq2_v77_original_handle(const native_handle_t *handle) {
    pthread_mutex_lock(&mutex);
    const native_handle_t *result=handle;
    for(auto &entry:entries)if(handle==reinterpret_cast<const native_handle_t*>(&entry.view)){
        result=entry.ahb?entry.original:nullptr;break;
    }
    for(auto &entry:native_entries)if(handle==reinterpret_cast<const native_handle_t*>(&entry.view)){
        result=entry.imported;break;
    }
    pthread_mutex_unlock(&mutex);return result;
}
extern "C" unsigned sq2_v77_ahb_stats(unsigned *leases,unsigned *owners,uint64_t *size) {
    unsigned count=0,l=0,o=0;pthread_mutex_lock(&mutex);
    for(const auto &entry:entries)if(entry.ahb){count++;l+=entry.leases;o+=entry.owners;}
    if(leases)*leases=l;if(owners)*owners=o;if(size)*size=bytes;
    pthread_mutex_unlock(&mutex);return count;
}

// WSI supplies a raw native handle, not an AHardwareBuffer. A private mapper5
// import keeps its real allocation/FDs alive, independently of the BufferQueue.
// Each image gets a distinct lease; never mutate the platform handle or borrow
// an AHB pointer from undocumented GraphicBuffer object offsets.
extern "C" int sq2_v77_native_prepare(const void *input,const void **view,void **lease,
                                       Sq2V77NativeInfo *info) {
    if(!view || !lease || !info)return -EINVAL;
    *view=nullptr;*lease=nullptr;*info={};
    const auto *original=sq2_v77_original_handle(static_cast<const native_handle_t*>(input));
    if(!original)return -EINVAL;
    AIMapper *m=sq2::mapper5();if(!m)return -ENODEV;
    buffer_handle_t imported=nullptr;
    AIMapper_Error imported_status=m->v5.importBuffer(original,&imported);
    if(imported_status!=AIMAPPER_ERROR_NONE || !imported)return -EINVAL;
    sq2::BufferInfo b;
    if(!color_metadata(imported,b)){
        m->v5.freeBuffer(imported);return -ENOTSUP;
    }
    int result=0;
    pthread_mutex_lock(&mutex);
    NativeEntry *entry=nullptr;
    for(auto &candidate:native_entries)if(!candidate.imported){entry=&candidate;break;}
    if(!entry || b.fd_size>limit_bytes-bytes-native_bytes)result=-ENOSPC;
    else{
        int fd=fcntl(b.fd,F_DUPFD_CLOEXEC,0);
        if(fd<0)result=-ENOMEM;
        else{
            *entry={m,imported,make_view(b,fd)};native_bytes+=b.fd_size;
            *view=&entry->view;*lease=entry;
            *info={static_cast<uint32_t>(b.width),static_cast<uint32_t>(b.height),1,b.format,
                   static_cast<uint32_t>(b.stride/4),b.fd_size};
        }
    }
    pthread_mutex_unlock(&mutex);
    if(result)m->v5.freeBuffer(imported);
    return result;
}
extern "C" void sq2_v77_native_finish(void *lease) {
    NativeEntry dead;
    pthread_mutex_lock(&mutex);
    NativeEntry *found=nullptr;
    for(auto &entry:native_entries)if(&entry==lease && entry.imported){found=&entry;break;}
    if(!found)invariant("native-finish-without-lease");
    dead=*found;native_bytes-=found->view.size;*found={};
    pthread_mutex_unlock(&mutex);
    close(dead.view.fd);
    if(dead.mapper->v5.freeBuffer(dead.imported)!=AIMAPPER_ERROR_NONE)
        invariant("native-free-import-failed");
}
extern "C" unsigned sq2_v77_native_stats(uint64_t *size) {
    unsigned count=0;pthread_mutex_lock(&mutex);
    for(const auto &entry:native_entries)if(entry.imported)count++;
    if(size)*size=native_bytes;
    pthread_mutex_unlock(&mutex);return count;
}

// Only the copied donor's imports are renamed. Calls by the platform and the
// adapter itself still resolve to the actual libnativewindow implementation.
extern "C" __attribute__((noinline)) const native_handle_t *SQ2NativeBuffer_getNativeHandle(const AHardwareBuffer *ahb) {
    uintptr_t site=caller(__builtin_return_address(0));
    switch(site){case 0xdc174:case 0x14b43c:case 0x14b528:case 0x1ce1d0:
        case 0x1f6f08:case 0x1f71a8:case 0x1f5860:break;
        default:invariant("get-handle-caller",site);}
    pthread_mutex_lock(&mutex);Entry *entry=find(ahb);
    if(!entry || (!entry->owners && site!=0xdc174))invariant("get-handle-without-owner",site);
    auto *view=reinterpret_cast<const native_handle_t*>(&entry->view);
    pthread_mutex_unlock(&mutex);return view;
}
extern "C" __attribute__((noinline)) void SQ2NativeBuffer_acquire(AHardwareBuffer *ahb) {
    uintptr_t site=caller(__builtin_return_address(0));
    if(site==0x1f6ea0){
        pthread_mutex_lock(&mutex);Entry *entry=find(ahb);
        if(!entry || !entry->leases || entry->owners==UINT_MAX)invariant("acquire-without-transaction",site);
        entry->owners++;pthread_mutex_unlock(&mutex);
    }else if(site!=0xdefd4)invariant("acquire-caller",site);
    AHardwareBuffer_acquire(ahb);
}
extern "C" __attribute__((noinline)) void SQ2NativeBuffer_release(AHardwareBuffer *ahb) {
    uintptr_t site=caller(__builtin_return_address(0));
    if(site!=0x1f5878)invariant("release-caller",site);
    pthread_mutex_lock(&mutex);Entry *entry=find(ahb);
    if(!entry || !entry->owners)invariant("release-without-owner",site);
    entry->owners--;Entry dead=remove_if_idle(entry);pthread_mutex_unlock(&mutex);
    AHardwareBuffer_release(ahb);dispose(dead);
}
extern "C" __attribute__((noinline)) int SQ2NativeBuffer_allocate(const AHardwareBuffer_Desc*,AHardwareBuffer **out) {
    uintptr_t site=caller(__builtin_return_address(0));
    if(site!=0x1f76cc)invariant("allocate-caller",site);
    if(out)*out=nullptr;
    // Export allocation is explicitly rejected by the outer Vulkan wrapper.
    // An unexpected internal attempt still returns a real allocation failure.
    return -ENOTSUP;
}
extern "C" int sq2_getmodule(const char *id,const hw_module_t **module) {
    if(!id || !module)return -EINVAL;
    *module=nullptr;
    // v77 otherwise sends its raw handle straight to old gralloc perform.
    // This platform has mapper5; no compatible QTI old-gralloc module exists.
    if(!std::strcmp(id,"gralloc"))return -ENOENT;
    return hw_get_module(id,module);
}
