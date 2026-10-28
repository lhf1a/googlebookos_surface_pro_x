/* Experimental process-local QTI metadata adapter. Load only as a dependency
 * of the isolated donor (or probe preload). No registered service/system HAL changes.
 * Mapper3 scope: unprotected linear RGBA8888, YV12 and verified NV12 buffers.
 * Mapper4 retains its RGBA-only scope. Other operations fail
 * explicitly; this is not a complete Android mapper implementation. */
#ifdef SQ2_QTI_MAPPER3
#include <vendor/qti/hardware/display/mapper/3.0/IQtiMapper.h>
#else
#include <vendor/qti/hardware/display/mapper/4.0/IQtiMapper.h>
#endif
#include <vendor/qti/hardware/display/mapperextensions/1.1/IQtiMapperExtensions.h>
#include <cstdio>
#include "quest2-mapper5.h"
#ifdef SQ2_QTI_MAPPER3
#include "quest2-yuv-metadata.h"
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#endif
#ifdef SQ2_QTI_MAPPER3
namespace am=android::hardware::graphics::mapper::V3_0;
namespace qm=vendor::qti::hardware::display::mapper::V3_0;
#else
namespace am=android::hardware::graphics::mapper::V4_0;
namespace qm=vendor::qti::hardware::display::mapper::V4_0;
#endif
namespace qe=vendor::qti::hardware::display::mapperextensions::V1_0;
namespace qe1=vendor::qti::hardware::display::mapperextensions::V1_1;
using android::hardware::Return;
using android::hardware::Void;
using android::hardware::hidl_handle;
using android::hardware::hidl_vec;
using android::sp;

namespace sq2 {
#ifdef SQ2_QTI_MAPPER3
// v77 EGL falls back to raw QTI-handle fields when this query is unsupported.
// A minigbm handle has width/height at the assumed CPU-base offset, corrupting
// the plane-offset arithmetic. Return an actual, stable mapping, not a fake
// pointer or an assumed private handle layout. This bounded process-local
// cache is tied to native-buffer proxy references in the isolated EGL wrapper.
struct AddressMappings {
    struct Entry {dev_t dev;ino_t ino;uint64_t size;void *base;unsigned refs;};
    pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
    Entry entries[128]={};unsigned count=0;uint64_t bytes=0;
    ~AddressMappings(){for(unsigned i=0;i<count;i++)munmap(entries[i].base,entries[i].size);}
    void reset(){
        pthread_mutex_lock(&mutex);
        for(unsigned i=0;i<count;i++)munmap(entries[i].base,entries[i].size);
        std::fprintf(stderr,"QTI_ADDRESS_RESET mappings=%u bytes=%llu\n",count,static_cast<unsigned long long>(bytes));
        count=0;bytes=0;
        pthread_mutex_unlock(&mutex);
    }
    unsigned references(){
        pthread_mutex_lock(&mutex);unsigned refs=0;
        for(unsigned i=0;i<count;i++)refs+=entries[i].refs;
        pthread_mutex_unlock(&mutex);return refs;
    }
    void release(const BufferInfo &b){
        struct stat st={};if(fstat(b.fd,&st))return;
        pthread_mutex_lock(&mutex);
        for(unsigned i=0;i<count;i++)if(entries[i].dev==st.st_dev&&entries[i].ino==st.st_ino&&entries[i].size==b.fd_size){
            if(entries[i].refs && --entries[i].refs==0){
                munmap(entries[i].base,entries[i].size);bytes-=entries[i].size;
                entries[i]=entries[--count];
                std::fprintf(stderr,"QTI_ADDRESS_RELEASE remaining=%u\n",count);
            }
            break;
        }
        pthread_mutex_unlock(&mutex);
    }
    void *get(const BufferInfo &b,bool retain=false){
        struct stat st={};if(fstat(b.fd,&st))return nullptr;
        pthread_mutex_lock(&mutex);
        void *base=nullptr;
        for(unsigned i=0;i<count;i++)if(entries[i].dev==st.st_dev&&entries[i].ino==st.st_ino&&entries[i].size==b.fd_size){base=entries[i].base;if(retain)entries[i].refs++;break;}
        if(!base&&count<128&&b.fd_size<=512ull*1024*1024-bytes){
            void *mapped=mmap(nullptr,b.fd_size,PROT_READ|PROT_WRITE,MAP_SHARED,b.fd,0);
            if(mapped!=MAP_FAILED){
                entries[count++]={st.st_dev,st.st_ino,b.fd_size,mapped,retain?1u:0u};bytes+=b.fd_size;base=mapped;
            }
        }
        pthread_mutex_unlock(&mutex);
        return base?static_cast<char*>(base)+b.offset:nullptr;
    }
};
static AddressMappings &address_mappings(){static AddressMappings instance;return instance;}
static bool supported_buffer(const native_handle_t *handle,BufferInfo &b){
    if(color_info(handle,b)&&b.format==1&&b.fourcc==0x34324241u&&!(b.usage&(1ull<<14)))return true;
    YuvInfo y;YuvLayout layout;
    if(!yuv_info(handle,y,layout))return false;
    b=y;return true;
}
// Explicit teardown hook. The caller MUST first destroy every image, texture,
// surface and context and terminate EGL. Not safe for per-image destruction.
extern "C" void sq2_qti_mapper3_reset_addresses(){address_mappings().reset();}
extern "C" unsigned sq2_qti_mapper3_address_refs(){return address_mappings().references();}
extern "C" bool sq2_qti_mapper3_retain_buffer(const native_handle_t *handle){
    BufferInfo b;return supported_buffer(handle,b)&&address_mappings().get(b,true);
}
extern "C" void sq2_qti_mapper3_release_buffer(const native_handle_t *handle){
    BufferInfo b;if(supported_buffer(handle,b))address_mappings().release(b);
}
#endif
static bool info(void *buffer,BufferInfo &b,const char *operation) {
    bool ok=color_info(static_cast<const native_handle_t*>(buffer),b)&&b.format==1&&
        b.fourcc==0x34324241u && b.sample_bits==32 && !(b.usage&(1ull<<14)) &&
        b.stride/4<=INT32_MAX && b.width<=INT32_MAX && b.height<=INT32_MAX &&
        b.stride%4==0 && b.offset%4==0 && b.width<=b.stride/4 && b.height<=b.plane_size/b.stride;
#ifdef SQ2_QTI_MAPPER3
    // QTI's allocated height is physical scanlines; StandardMetadata height
    // and plane heightInSamples are logical in this minigbm implementation.
    ok=ok && b.plane_size%b.stride==0 && b.plane_size/b.stride<=INT32_MAX;
    if(!ok){YuvInfo y;YuvLayout layout;ok=yuv_info(static_cast<const native_handle_t*>(buffer),y,layout);if(ok)b=y;}
#endif
    std::fprintf(stderr,"QTI_BRIDGE %s %s\n",operation,ok?"OK":"BAD_BUFFER");
    return ok;
}
struct Extensions final : qe1::IQtiMapperExtensions {
#define GETTER(name, expression, type) \
    Return<void> name(void *buffer,name##_cb cb) override { \
        BufferInfo b;bool ok=info(buffer,b,#name); \
        cb(ok?qe::Error::NONE:qe::Error::BAD_BUFFER,static_cast<type>(ok?(expression):0));return Void(); }
    GETTER(getMapSecureBufferFlag, false, bool)
    GETTER(getInterlacedFlag, false, bool)
    GETTER(getWidth, b.stride*8/b.sample_bits, int32_t)
#ifdef SQ2_QTI_MAPPER3
    GETTER(getHeight, b.plane_size/b.stride, int32_t)
#else
    GETTER(getHeight, b.height, int32_t)
#endif
    GETTER(getOffset, b.offset, uint64_t)
    // Metadata allocation size can include an additional metadata allocation.
    // This QTI field describes the pixel fd that getFd returns.
    GETTER(getSize, b.fd_size, uint64_t)
    GETTER(getUnalignedWidth, b.width, int32_t)
    GETTER(getUnalignedHeight, b.height, int32_t)
    GETTER(getLayerCount, b.layers, uint64_t)
    GETTER(getId, b.id, uint64_t)
    GETTER(getUsageFlags, b.usage, uint64_t)
#ifdef SQ2_QTI_MAPPER3
    // Flexible 420_888 is a request, not a QTI concrete layout enum. Only after
    // yuv_info verifies actual NV12 planes, expose QTI YCbCr_420_SP (0x109).
    GETTER(getFormat, b.format==35?0x109:b.format, int32_t)
#else
    GETTER(getFormat, b.format, int32_t)
#endif
#ifdef SQ2_QTI_MAPPER3
    // v77 op 24 extracts bit 3 and refuses window buffers without it, before
    // calling its fd/size/offset MapMemory path. For validated minigbm DMA-BUFs
    // this is a legacy QTI fd-backed-import compatibility bit, not a claim that
    // allocation used /dev/ion. Keep compression/security/layout bits clear.
    GETTER(getPrivateFlags, 0x8, int32_t)
#else
    GETTER(getPrivateFlags, 0, int32_t)
#endif
#undef GETTER
    Return<void> getFd(void *buffer,getFd_cb cb) override {
        BufferInfo b;bool ok=info(buffer,b,__func__);
        cb(ok?qe::Error::NONE:qe::Error::BAD_BUFFER,ok?b.fd:-1);return Void();
    }
    Return<void> getColorSpace(void*,getColorSpace_cb cb) override {cb(qe::Error::UNSUPPORTED,0);return Void();}
    Return<void> getCustomDimensions(void *buffer,getCustomDimensions_cb cb) override {
        BufferInfo b;bool ok=info(buffer,b,__func__);
#ifdef SQ2_QTI_MAPPER3
        cb(ok?qe::Error::NONE:qe::Error::BAD_BUFFER,ok?b.stride*8/b.sample_bits:0,ok?b.plane_size/b.stride:0);
#else
        cb(ok?qe::Error::NONE:qe::Error::BAD_BUFFER,ok?b.stride/4:0,ok?b.height:0);
#endif
        return Void();
    }
    Return<void> getRgbDataAddress(void *buffer,getRgbDataAddress_cb cb) override {
#ifdef SQ2_QTI_MAPPER3
        BufferInfo b;bool ok=info(buffer,b,__func__);
        void *address=ok?address_mappings().get(b):nullptr;
        cb(address?qe::Error::NONE:qe::Error::BAD_BUFFER,address);
#else
        (void)buffer;cb(qe::Error::UNSUPPORTED,nullptr);
#endif
        return Void();
    }
    Return<void> calculateBufferAttributes(int32_t,int32_t,int32_t,uint64_t,calculateBufferAttributes_cb cb) override {
        std::fprintf(stderr,"QTI_BRIDGE calculateBufferAttributes UNSUPPORTED\n");cb(qe::Error::UNSUPPORTED,0,0,false);return Void();
    }
    Return<void> getYuvPlaneInfo(void *buffer,getYuvPlaneInfo_cb cb) override {
#ifdef SQ2_QTI_MAPPER3
        YuvInfo b;YuvLayout l;
        bool ok=yuv_info(static_cast<const native_handle_t*>(buffer),b,l);
        char *base=ok?static_cast<char*>(address_mappings().get(b)):nullptr;
        if(!base){cb(qe::Error::BAD_BUFFER,{});return Void();}
        // Original v77 mapper returns two field layouts, zero second field for
        // progressive images. Plane addresses are real CPU mapping+metadata.
        hidl_vec<qe::YCbCrLayout> layouts;layouts.resize(2);
        layouts[0]={base+l.y,base+l.cb,base+l.cr,static_cast<uint32_t>(l.y_stride),
            static_cast<uint32_t>(l.c_stride),static_cast<uint32_t>(l.chroma_step)};
        layouts[1]={};
        std::fprintf(stderr,"QTI_YUV_LAYOUT y=%llu cb=%llu cr=%llu strides=%llu,%llu step=%llu\n",
            (unsigned long long)l.y,(unsigned long long)l.cb,(unsigned long long)l.cr,
            (unsigned long long)l.y_stride,(unsigned long long)l.c_stride,(unsigned long long)l.chroma_step);
        cb(qe::Error::NONE,layouts);
#else
        (void)buffer;cb(qe::Error::UNSUPPORTED,{});
#endif
        return Void();
    }
    Return<qe::Error> setSingleBufferMode(void*,bool) override {return qe::Error::UNSUPPORTED;}
    Return<void> getCustomFormatFlags(int32_t,uint64_t,getCustomFormatFlags_cb cb) override {cb(qe::Error::UNSUPPORTED,0,0);return Void();}
    Return<void> getSurfaceMetadata(void*,getSurfaceMetadata_cb cb) override {cb(qe::Error::UNSUPPORTED,nullptr);return Void();}
    Return<void> getFormatLayout(int32_t,uint64_t,int32_t,int32_t,int32_t,getFormatLayout_cb cb) override {
        cb(qe::Error::UNSUPPORTED,0,{});return Void();
    }
    Return<qe::Error> getSurfaceMetadata_V1(void*,void*) override {
        std::fprintf(stderr,"QTI_BRIDGE getSurfaceMetadata_V1 UNSUPPORTED\n");return qe::Error::UNSUPPORTED;
    }
};
static sp<Extensions> extension() {static sp<Extensions> instance=new Extensions;return instance;}
struct Mapper final : qm::IQtiMapper {
    Return<void> createDescriptor(const BufferDescriptorInfo&,createDescriptor_cb cb) override {cb(am::Error::UNSUPPORTED,{});return Void();}
    Return<void> importBuffer(const hidl_handle &handle,importBuffer_cb cb) override {
        AIMapper *m=mapper5();buffer_handle_t imported=nullptr;
        AIMapper_Error error=m?m->v5.importBuffer(handle.getNativeHandle(),&imported):AIMAPPER_ERROR_NO_RESOURCES;
        cb(static_cast<am::Error>(error),const_cast<native_handle_t*>(imported));return Void();
    }
    Return<am::Error> freeBuffer(void *buffer) override {
        AIMapper *m=mapper5();return m?static_cast<am::Error>(m->v5.freeBuffer(static_cast<buffer_handle_t>(buffer))):am::Error::NO_RESOURCES;
    }
    Return<am::Error> validateBufferSize(void*,const BufferDescriptorInfo&,uint32_t) override {return am::Error::UNSUPPORTED;}
    Return<void> getTransportSize(void *buffer,getTransportSize_cb cb) override {
        AIMapper *m=mapper5();uint32_t fds=0,ints=0;
        AIMapper_Error error=m?m->v5.getTransportSize(static_cast<buffer_handle_t>(buffer),&fds,&ints):AIMAPPER_ERROR_NO_RESOURCES;
        cb(static_cast<am::Error>(error),fds,ints);return Void();
    }
#ifdef SQ2_QTI_MAPPER3
    // A donor may pass an AHB's native handle directly. Keep our own mapper5
    // import alive for the whole CPU access, rather than passing a foreign
    // handle or freeing a temporary import immediately after lock().
    struct ActiveLock {void *original;buffer_handle_t imported;bool busy;};
    pthread_mutex_t lock_mutex=PTHREAD_MUTEX_INITIALIZER;
    ActiveLock active_locks[128]={};
    ActiveLock *reserve_lock(void *buffer){
        pthread_mutex_lock(&lock_mutex);
        ActiveLock *slot=nullptr;
        for(auto &entry:active_locks)if(!entry.original){entry={buffer,nullptr,true};slot=&entry;break;}
        pthread_mutex_unlock(&lock_mutex);
        return slot;
    }
    void finish_lock(ActiveLock *slot,buffer_handle_t imported){
        pthread_mutex_lock(&lock_mutex);
        if(imported){slot->imported=imported;slot->busy=false;}
        else *slot={};
        pthread_mutex_unlock(&lock_mutex);
    }
    ActiveLock *take_lock(void *buffer){
        pthread_mutex_lock(&lock_mutex);
        ActiveLock *slot=nullptr;
        for(auto &entry:active_locks)if(buffer && entry.original==buffer && entry.imported && !entry.busy){
            entry.busy=true;slot=&entry;break;
        }
        pthread_mutex_unlock(&lock_mutex);
        return slot;
    }
    Return<void> lock(void *buffer,uint64_t cpu_usage,const Rect &region,
                      const hidl_handle &acquire_fence,lock_cb cb) override {
        std::fprintf(stderr,"QTI_BRIDGE mapper3 lock usage=0x%llx region=%d,%d,%d,%d\n",
            static_cast<unsigned long long>(cpu_usage),region.left,region.top,region.width,region.height);
        BufferInfo b;AIMapper *m=mapper5();
        if(!m){cb(am::Error::NO_RESOURCES,nullptr,0,0);return Void();}
        if(!info(buffer,b,"mapper3 lock") || b.stride>INT32_MAX){
            cb(am::Error::BAD_BUFFER,nullptr,0,0);return Void();
        }
        const int64_t right=static_cast<int64_t>(region.left)+region.width;
        const int64_t bottom=static_cast<int64_t>(region.top)+region.height;
        if(region.left<0 || region.top<0 || region.width<0 || region.height<0 ||
           right>INT32_MAX || bottom>INT32_MAX){
            cb(am::Error::BAD_VALUE,nullptr,0,0);return Void();
        }
        int fence_fd=-1;
        const native_handle_t *fence=acquire_fence.getNativeHandle();
        if(fence){
            if(fence->numInts!=0 || fence->numFds<0 || fence->numFds>1 ||
               (fence->numFds==1 && fence->data[0]<0)){
                cb(am::Error::BAD_VALUE,nullptr,0,0);return Void();
            }
            if(fence->numFds==1){
                // HIDL borrows this handle. Mapper5 consumes its fd even on
                // failure, so transfer a duplicate, never the caller's fd.
                fence_fd=fcntl(fence->data[0],F_DUPFD_CLOEXEC,0);
                if(fence_fd<0){cb(am::Error::NO_RESOURCES,nullptr,0,0);return Void();}
            }
        }
        ActiveLock *slot=reserve_lock(buffer);
        if(!slot){
            if(fence_fd>=0)close(fence_fd);
            cb(am::Error::NO_RESOURCES,nullptr,0,0);return Void();
        }
        buffer_handle_t imported=nullptr;
        AIMapper_Error error=m->v5.importBuffer(static_cast<buffer_handle_t>(buffer),&imported);
        std::fprintf(stderr,"QTI_BRIDGE mapper3 lock import result=%d handle=%p\n",static_cast<int>(error),imported);
        if(error!=AIMAPPER_ERROR_NONE){
            if(fence_fd>=0)close(fence_fd);
            finish_lock(slot,nullptr);
            cb(static_cast<am::Error>(error),nullptr,0,0);return Void();
        }
        ARect rect={region.left,region.top,static_cast<int32_t>(right),static_cast<int32_t>(bottom)};
        void *data=nullptr;
        // No bookkeeping mutex is held while mapper5 waits on a fence.
        error=m->v5.lock(imported,cpu_usage,rect,fence_fd,&data);
        std::fprintf(stderr,"QTI_BRIDGE mapper3 lock result=%d data=%p bpp=%llu stride=%llu\n",
            static_cast<int>(error),data,static_cast<unsigned long long>(b.sample_bits/8),
            static_cast<unsigned long long>(b.stride));
        bool ok=error==AIMAPPER_ERROR_NONE;
        if(!ok)m->v5.freeBuffer(imported);
        finish_lock(slot,ok?imported:nullptr);
        cb(static_cast<am::Error>(error),ok?data:nullptr,
            ok?static_cast<int32_t>(b.sample_bits/8):0,ok?static_cast<int32_t>(b.stride):0);
        return Void();
    }
    Return<void> lockYCbCr(void *buffer,uint64_t usage,const Rect &region,const hidl_handle &fence,lockYCbCr_cb cb) override {
        YuvInfo b;YuvLayout l;
        if(!yuv_info(static_cast<const native_handle_t*>(buffer),b,l)){cb(am::Error::BAD_BUFFER,{});return Void();}
        return lock(buffer,usage,region,fence,[&](am::Error error,void *data,int32_t,int32_t){
            am::YCbCrLayout result={};
            if(error==am::Error::NONE){
                char *base=static_cast<char*>(data);
                result.y=base+l.y;result.cb=base+l.cb;result.cr=base+l.cr;
                result.yStride=l.y_stride;result.cStride=l.c_stride;result.chromaStep=l.chroma_step;
            }
            cb(error,result);
        });
    }
#else
    Return<void> lock(void*,uint64_t,const Rect&,const hidl_handle&,lock_cb cb) override {cb(am::Error::UNSUPPORTED,nullptr);return Void();}
#endif
#ifdef SQ2_QTI_MAPPER3
    Return<void> unlock(void *buffer,unlock_cb cb) override {
        std::fprintf(stderr,"QTI_BRIDGE mapper3 unlock begin\n");
        AIMapper *m=mapper5();int release_fd=-1;
        if(!m){cb(am::Error::NO_RESOURCES,{});return Void();}
        ActiveLock *slot=take_lock(buffer);
        if(!slot){
            std::fprintf(stderr,"QTI_BRIDGE mapper3 unlock no active import\n");
            cb(am::Error::BAD_BUFFER,{});return Void();
        }
        AIMapper_Error error=m->v5.unlock(slot->imported,&release_fd);
        AIMapper_Error free_error=m->v5.freeBuffer(slot->imported);
        finish_lock(slot,nullptr);
        std::fprintf(stderr,"QTI_BRIDGE mapper3 unlock free result=%d\n",static_cast<int>(free_error));
        if(error==AIMAPPER_ERROR_NONE && free_error!=AIMAPPER_ERROR_NONE)error=free_error;
        std::fprintf(stderr,"QTI_BRIDGE mapper3 unlock result=%d fence=%d\n",static_cast<int>(error),release_fd);
        if(error==AIMAPPER_ERROR_NONE && release_fd>=0){
            // The callback borrows this handle; a client retaining its fence
            // must duplicate it, just as for a passthrough HIDL mapper HAL.
            alignas(native_handle_t) unsigned char storage[sizeof(native_handle_t)+sizeof(int)]={};
            auto *fence=reinterpret_cast<native_handle_t*>(storage);
            fence->version=sizeof(native_handle_t);fence->numFds=1;fence->data[0]=release_fd;
            hidl_handle handle;handle.setTo(fence,false);
            cb(am::Error::NONE,handle);
        }else cb(static_cast<am::Error>(error),{});
        if(release_fd>=0)close(release_fd);
        return Void();
    }
#else
    Return<void> unlock(void*,unlock_cb cb) override {cb(am::Error::UNSUPPORTED,{});return Void();}
#endif
    Return<void> isSupported(const BufferDescriptorInfo&,isSupported_cb cb) override {cb(am::Error::UNSUPPORTED,false);return Void();}
#ifndef SQ2_QTI_MAPPER3
    Return<void> flushLockedBuffer(void*,flushLockedBuffer_cb cb) override {cb(am::Error::UNSUPPORTED,{});return Void();}
    Return<am::Error> rereadLockedBuffer(void*) override {return am::Error::UNSUPPORTED;}
    Return<void> get(void*,const MetadataType&,get_cb cb) override {cb(am::Error::UNSUPPORTED,{});return Void();}
    Return<am::Error> set(void*,const MetadataType&,const hidl_vec<uint8_t>&) override {return am::Error::UNSUPPORTED;}
    Return<void> getFromBufferDescriptorInfo(const BufferDescriptorInfo&,const MetadataType&,getFromBufferDescriptorInfo_cb cb) override {cb(am::Error::UNSUPPORTED,{});return Void();}
    Return<void> listSupportedMetadataTypes(listSupportedMetadataTypes_cb cb) override {cb(am::Error::UNSUPPORTED,{});return Void();}
    Return<void> dumpBuffer(void*,dumpBuffer_cb cb) override {cb(am::Error::UNSUPPORTED,{});return Void();}
    Return<void> dumpBuffers(dumpBuffers_cb cb) override {cb(am::Error::UNSUPPORTED,{});return Void();}
    Return<void> getReservedRegion(void*,getReservedRegion_cb cb) override {cb(am::Error::UNSUPPORTED,nullptr,0);return Void();}
#endif
    Return<void> getMapperExtensions(getMapperExtensions_cb cb) override {
        std::fprintf(stderr,"QTI_BRIDGE extensions requested\n");cb(am::Error::NONE,extension());return Void();
    }
};
static sp<Mapper> mapper() {static sp<Mapper> instance=new Mapper;return instance;}
}

// Only these three entrypoints are interposed in the isolated probe process.
sp<am::IMapper> am::IMapper::getService(const std::string&,bool) {
#ifdef SQ2_QTI_MAPPER3
    std::fprintf(stderr,"QTI_BRIDGE local mapper3 requested\n");return sq2::mapper();
#else
    std::fprintf(stderr,"QTI_BRIDGE local mapper4 requested\n");return sq2::mapper();
#endif
}
Return<sp<qm::IQtiMapper>> qm::IQtiMapper::castFrom(const sp<am::IMapper> &parent,bool) {
    if(parent.get()!=sq2::mapper().get())return sp<qm::IQtiMapper>();
    return sp<qm::IQtiMapper>(sq2::mapper());
}
Return<sp<qe1::IQtiMapperExtensions>> qe1::IQtiMapperExtensions::castFrom(const sp<qe::IQtiMapperExtensions> &parent,bool) {
    if(parent.get()!=sq2::extension().get())return sp<qe1::IQtiMapperExtensions>();
    return sp<qe1::IQtiMapperExtensions>(sq2::extension());
}
