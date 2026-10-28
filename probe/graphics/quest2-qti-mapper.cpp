/* Experimental process-local QTI metadata adapter. Load only as a dependency
 * of the isolated donor (or probe preload). No registered service/system HAL changes.
 * Initial scope: unprotected linear RGBA8888 buffers. Other operations fail
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
// cache is for the isolated probe; system adoption needs image-lifetime hooks.
struct AddressMappings {
    struct Entry {dev_t dev;ino_t ino;uint64_t size;void *base;};
    pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
    Entry entries[128]={};unsigned count=0;uint64_t bytes=0;
    ~AddressMappings(){for(unsigned i=0;i<count;i++)munmap(entries[i].base,entries[i].size);}
    void *get(const BufferInfo &b){
        struct stat st={};if(fstat(b.fd,&st))return nullptr;
        pthread_mutex_lock(&mutex);
        void *base=nullptr;
        for(unsigned i=0;i<count;i++)if(entries[i].dev==st.st_dev&&entries[i].ino==st.st_ino&&entries[i].size==b.fd_size){base=entries[i].base;break;}
        if(!base&&count<128&&b.fd_size<=512ull*1024*1024-bytes){
            void *mapped=mmap(nullptr,b.fd_size,PROT_READ|PROT_WRITE,MAP_SHARED,b.fd,0);
            if(mapped!=MAP_FAILED){
                entries[count++]={st.st_dev,st.st_ino,b.fd_size,mapped};bytes+=b.fd_size;base=mapped;
            }
        }
        pthread_mutex_unlock(&mutex);
        return base?static_cast<char*>(base)+b.offset:nullptr;
    }
};
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
    GETTER(getFormat, b.format, int32_t)
    GETTER(getPrivateFlags, 0, int32_t)
#undef GETTER
    Return<void> getFd(void *buffer,getFd_cb cb) override {
        BufferInfo b;bool ok=info(buffer,b,__func__);
        cb(ok?qe::Error::NONE:qe::Error::BAD_BUFFER,ok?b.fd:-1);return Void();
    }
    Return<void> getColorSpace(void*,getColorSpace_cb cb) override {cb(qe::Error::UNSUPPORTED,0);return Void();}
    Return<void> getCustomDimensions(void *buffer,getCustomDimensions_cb cb) override {
        BufferInfo b;bool ok=info(buffer,b,__func__);
#ifdef SQ2_QTI_MAPPER3
        cb(ok?qe::Error::NONE:qe::Error::BAD_BUFFER,ok?b.stride/4:0,ok?b.plane_size/b.stride:0);
#else
        cb(ok?qe::Error::NONE:qe::Error::BAD_BUFFER,ok?b.stride/4:0,ok?b.height:0);
#endif
        return Void();
    }
    Return<void> getRgbDataAddress(void *buffer,getRgbDataAddress_cb cb) override {
#ifdef SQ2_QTI_MAPPER3
        BufferInfo b;bool ok=info(buffer,b,__func__);
        static AddressMappings mappings;
        void *address=ok?mappings.get(b):nullptr;
        cb(address?qe::Error::NONE:qe::Error::BAD_BUFFER,address);
#else
        (void)buffer;cb(qe::Error::UNSUPPORTED,nullptr);
#endif
        return Void();
    }
    Return<void> calculateBufferAttributes(int32_t,int32_t,int32_t,uint64_t,calculateBufferAttributes_cb cb) override {
        std::fprintf(stderr,"QTI_BRIDGE calculateBufferAttributes UNSUPPORTED\n");cb(qe::Error::UNSUPPORTED,0,0,false);return Void();
    }
    Return<void> getYuvPlaneInfo(void*,getYuvPlaneInfo_cb cb) override {cb(qe::Error::UNSUPPORTED,{});return Void();}
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
    Return<void> lockYCbCr(void*,uint64_t,const Rect&,const hidl_handle&,lockYCbCr_cb cb) override {cb(am::Error::UNSUPPORTED,{});return Void();}
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
