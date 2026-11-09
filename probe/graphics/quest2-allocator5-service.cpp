/* AIDL allocator2 endpoint for the direct QTI gralloc backend.
 * Nonsecure RGB (linear/UBWC) and linear YV12/semiplanar YUV coverage. Never substitutes unsupported options. */
#include <aidl/android/hardware/graphics/allocator/BnAllocator.h>
#include <aidl/android/hardware/graphics/allocator/AllocationError.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <fcntl.h>
#include <mutex>
#include <cstdio>
#include <cstring>
#include <cassert>
#include "quest2-gralloc-backend.h"
#include "quest2-gralloc-metadata.h"
namespace aa=aidl::android::hardware::graphics::allocator;
using ndk::ScopedAStatus;
class QuestAllocator final:public aa::BnAllocator {
    QuestGralloc backend;
    std::mutex mutex;
    static bool supports(const aa::BufferDescriptorInfo &d) {
        uint64_t usage=static_cast<uint64_t>(d.usage);
        uint64_t read=usage&0xf,write=usage&0xf0;
        // Preserve QTI's UBWC hint; CPU access can make the real layout linear.
        constexpr uint64_t qti_alloc_ubwc=1ULL<<28,qti_uncached=1ULL<<29;
        // SoftwareRenderer keeps the legacy external-display routing hint on CPU YV12 output.
        constexpr uint64_t external_display=0x2000;
        return d.width>0 && d.height>0 && d.layerCount==1 && d.reservedSize==0 && d.additionalOptions.empty() &&
            QuestGralloc::supportedFormat(int(d.format)) &&
            (!QuestGralloc::yuvFormat(int(d.format)) || (!(d.width&1) && !(d.height&1))) &&
            !(usage&~(0x1b33ULL|external_display|uint64_t(aidl::android::hardware::graphics::common::BufferUsage::VIDEO_DECODER)|qti_alloc_ubwc|qti_uncached)) && (!read || read==2 || read==3) && (!write || write==0x20 || write==0x30) &&
            (!gralloc::IsUBwcEnabled(int(d.format),usage) ||
             (!QuestGralloc::yuvFormat(int(d.format)) && !(usage&0xff)));
    }
    static ScopedAStatus failure(aa::AllocationError e){return ScopedAStatus::fromServiceSpecificError(static_cast<int32_t>(e));}
public:
    ScopedAStatus allocate(const std::vector<uint8_t> &,int32_t,aa::AllocationResult *) override {
        return failure(aa::AllocationError::UNSUPPORTED);
    }
    ScopedAStatus allocate2(const aa::BufferDescriptorInfo &d,int32_t count,aa::AllocationResult *out) override {
#ifdef QUEST2_ALLOCATOR_TEST_TRACE
        printf("ALLOCATOR5_REQUEST width=%d height=%d format=%x usage=%llx layers=%d reserved=%lld options=%zu count=%d\n",
            d.width,d.height,int(d.format),(unsigned long long)d.usage,d.layerCount,
            (long long)d.reservedSize,d.additionalOptions.size(),count);
#endif
        out->buffers.clear();out->stride=0;
        if(count<=0 || d.width<=0 || d.height<=0 || d.layerCount<=0 || d.reservedSize<0)
            return failure(aa::AllocationError::BAD_DESCRIPTOR);
        if(!supports(d))return failure(aa::AllocationError::UNSUPPORTED);
        std::lock_guard<std::mutex> guard(mutex);
        if(!backend.device)return failure(aa::AllocationError::NO_RESOURCES);
        aa::AllocationResult result;
        for(int32_t i=0;i<count;i++) {
            buffer_handle_t handle=nullptr;
            int e=backend.allocate(d.width,d.height,int(d.format),uint64_t(d.usage),&handle);
            if(e || !handle)return failure(aa::AllocationError::NO_RESOURCES);
            QuestGeometry g;
            if(!backend.geometry(handle,g) || g.stride>INT32_MAX || (i && result.stride!=int32_t(g.stride))) {
                backend.release(handle);return failure(aa::AllocationError::NO_RESOURCES);
            }
#ifdef QUEST2_ALLOCATOR_TEST_TRACE
            printf("ALLOCATOR5_GEOMETRY index=%d id=%llu width=%u height=%u layers=%u format=0x%x "
                   "producer_usage=0x%llx stride=%u scan=%u size=%u yuv=%d ubwc=%d sample_bytes=%u "
                   "plane_stride=%d plane_stride_bytes=%d plane_scanlines=%d plane_offset=%u plane_size=%u plane_step=%d "
                   "chroma_stride=%d chroma_stride_bytes=%d chroma_scanlines=%d chroma_offset=%u chroma_size=%u chroma_step=%d\n",
                i,(unsigned long long)g.id,g.width,g.height,g.layers,unsigned(g.format),
                (unsigned long long)g.usage,g.stride,g.scan,g.size,g.yuv,g.ubwc,g.sample_bytes,
                g.plane.stride,g.plane.stride_bytes,g.plane.scanlines,g.plane.offset,g.plane.size,g.plane.step,
                g.chroma.stride,g.chroma.stride_bytes,g.chroma.scanlines,g.chroma.offset,g.chroma.size,g.chroma.step);
#endif
            QuestMetadata metadata(handle);
            const char *name=reinterpret_cast<const char *>(d.name.data());
            if(!metadata.initializeName(std::string(name,strnlen(name,d.name.size())))) {
                backend.release(handle);return failure(aa::AllocationError::NO_RESOURCES);
            }
            aidl::android::hardware::common::NativeHandle native;
            bool copied=true;
            for(int n=0;n<handle->numFds;n++) {
                int fd=fcntl(handle->data[n],F_DUPFD_CLOEXEC,0);
                if(fd<0){copied=false;break;}
                native.fds.emplace_back(fd);
            }
            if(copied)native.ints.assign(handle->data+handle->numFds,handle->data+handle->numFds+handle->numInts);
            int released=backend.release(handle);
            if(!copied || released)return failure(aa::AllocationError::NO_RESOURCES);
            result.stride=g.stride;result.buffers.push_back(std::move(native));
        }
        *out=std::move(result);return ScopedAStatus::ok();
    }
    ScopedAStatus isSupported(const aa::BufferDescriptorInfo &d,bool *out) override {
        *out=backend.device && supports(d);return ScopedAStatus::ok();
    }
    ScopedAStatus getIMapperLibrarySuffix(std::string *out) override {*out="quest2";return ScopedAStatus::ok();}
};
int main(int argc,char **argv) {
    setvbuf(stdout,nullptr,_IONBF,0);
    auto service=ndk::SharedRefBase::make<QuestAllocator>();
    if(argc==2 && !strcmp(argv[1],"--selftest")) {
        aa::BufferDescriptorInfo d{};d.width=129;d.height=65;d.layerCount=1;
        d.format=aidl::android::hardware::graphics::common::PixelFormat::RGBA_8888;
        d.usage=static_cast<aidl::android::hardware::graphics::common::BufferUsage>(0x133);
        bool supported=false;assert(service->isSupported(d,&supported).isOk() && supported);
        aa::AllocationResult result;assert(service->allocate2(d,3,&result).isOk() && result.buffers.size()==3 && result.stride>=129);
        for(const auto &h:result.buffers)assert(h.fds.size()==2 && h.ints.size()==26);
        d.usage=static_cast<aidl::android::hardware::graphics::common::BufferUsage>(0x10000103);
        assert(service->isSupported(d,&supported).isOk() && supported);
        assert(service->allocate2(d,1,&result).isOk() && result.buffers.size()==1);
        d.usage=static_cast<aidl::android::hardware::graphics::common::BufferUsage>(0x10000300);
        assert(service->isSupported(d,&supported).isOk() && supported);
        assert(service->allocate2(d,1,&result).isOk() && result.buffers.size()==1);
        d.usage=static_cast<aidl::android::hardware::graphics::common::BufferUsage>(0x1a00);
        assert(service->isSupported(d,&supported).isOk() && supported);
        assert(service->allocate2(d,1,&result).isOk() && result.buffers.size()==1);
        d.usage=static_cast<aidl::android::hardware::graphics::common::BufferUsage>(0x8000000000000000ULL);
        assert(service->isSupported(d,&supported).isOk() && !supported);
        assert(!service->allocate2(d,1,&result).isOk() && result.buffers.empty());
        d.usage=static_cast<aidl::android::hardware::graphics::common::BufferUsage>(0x4103);
        assert(service->isSupported(d,&supported).isOk() && !supported);
        d.usage=static_cast<aidl::android::hardware::graphics::common::BufferUsage>(0x133);
        d.width=320;d.height=240;
        d.format=static_cast<aidl::android::hardware::graphics::common::PixelFormat>(0x7fa30c04);
        d.usage=static_cast<aidl::android::hardware::graphics::common::BufferUsage>(0x20400103);
        assert(service->isSupported(d,&supported).isOk() && supported);
        assert(service->allocate2(d,1,&result).isOk() && result.buffers.size()==1);
        d.usage=static_cast<aidl::android::hardware::graphics::common::BufferUsage>(0x20404103);
        assert(service->isSupported(d,&supported).isOk() && !supported);
        d.usage=static_cast<aidl::android::hardware::graphics::common::BufferUsage>(0x20400103);
        d.reservedSize=1;assert(service->isSupported(d,&supported).isOk() && !supported);
        assert(!service->allocate2(d,1,&result).isOk() && result.buffers.empty());
        std::string suffix;assert(service->getIMapperLibrarySuffix(&suffix).isOk() && suffix=="quest2");
        puts("ALLOCATOR5_LOCAL_SELFTEST_PASS remote_binder=0");return 0;
    }
    if(argc!=2 || (strcmp(argv[1],"default") && strcmp(argv[1],"quest2")))return 64;
    ABinderProcess_setThreadPoolMaxThreadCount(4);
    std::string name=std::string(aa::IAllocator::descriptor)+"/"+argv[1];
    binder_status_t status=AServiceManager_addService(service->asBinder().get(),name.c_str());
    printf("ALLOCATOR5_REGISTER service=%s status=%d\n",name.c_str(),status);
    if(status!=STATUS_OK)return 1;
    ABinderProcess_joinThreadPool();return 1;
}
