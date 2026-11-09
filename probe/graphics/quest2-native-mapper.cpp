/* Original mapper with bounded RGB contract repair; private and HAL builds. */
#include <vendor/qti/hardware/display/mapper/3.0/IQtiMapper.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <poll.h>
#include <cerrno>
namespace am = android::hardware::graphics::mapper::V3_0;
namespace qm = vendor::qti::hardware::display::mapper::V3_0;
using android::sp;
#include "quest2-native-rgb-stride.h"
#include "quest2-native-mapper-compat.h"
static sp<am::IMapper> native_mapper() {
    static sp<am::IMapper> instance = [] {
        #ifdef QUEST2_MAPPER_HAL
        const char *library_name="libquest2-original-mapper3.so";
#else
        const char *library_name="android.hardware.graphics.mapper@3.0-impl-qti-display.so";
#endif
        void *library = dlopen(library_name, RTLD_NOW | RTLD_LOCAL);
        if (!library) { std::fprintf(stderr, "NATIVE_MAPPER_LOAD_FAIL %s\n", dlerror()); std::abort(); }
        auto fetch = reinterpret_cast<am::IMapper *(*)(const char *)>(dlsym(library, "HIDL_FETCH_IMapper"));
        assert(fetch);
        Dl_info origin{}; assert(dladdr(reinterpret_cast<void *>(fetch), &origin));
        std::fprintf(stderr, "NATIVE_MAPPER_FACTORY %s\n", origin.dli_fname);
        sp<am::IMapper> mapper = fetch("default");
        assert(mapper && !mapper->isRemote());
        auto cast=qm::IQtiMapper::castFrom(mapper,true);assert(cast.isOk());
        sp<qm::IQtiMapper> qti=cast;assert(qti);
        return sp<am::IMapper>(new NativeMapperCompat(qti));
    }();
    return instance;
}
#ifdef QUEST2_MAPPER_HAL
extern "C" am::IMapper *HIDL_FETCH_IMapper(const char *name) {
    if(!name || std::strcmp(name,"default"))return nullptr;
    return native_mapper().get();
}
#else
sp<am::IMapper> am::IMapper::getService(const std::string &name, bool) {
    if (name != "default") return nullptr;
    return native_mapper();
}
#endif
extern "C" uint64_t quest2_native_mapper_check(const native_handle_t *handle, unsigned w, unsigned h, unsigned stride, int format) {
    auto mapper = native_mapper();
    auto cast = qm::IQtiMapper::castFrom(mapper, true);
    assert(cast.isOk());
    sp<qm::IQtiMapper> qti = cast;
    assert(qti);
    void *imported = nullptr;
    auto result = mapper->importBuffer(handle, [&](am::Error error, void *buffer) {
        assert(error == am::Error::NONE && buffer); imported = buffer;
    });
    assert(result.isOk() && imported);
    namespace qe=vendor::qti::hardware::display::mapperextensions::V1_0;
    sp<qe::IQtiMapperExtensions> extension;
    auto ext=qti->getMapperExtensions([&](am::Error error,const sp<qe::IQtiMapperExtensions> &v){assert(error==am::Error::NONE && v);extension=v;});assert(ext.isOk() && extension);
    uint64_t usage=0;bool got_usage=false;
    auto flags=extension->getUsageFlags(imported,[&](qe::Error error,uint64_t v){assert(error==qe::Error::NONE);usage=v;got_usage=true;});assert(flags.isOk() && got_usage);
    am::IMapper::BufferDescriptorInfo info{};
    info.width=w; info.height=h; info.layerCount=1;
    info.format=static_cast<android::hardware::graphics::common::V1_2::PixelFormat>(format);
    info.usage=usage;
    auto valid=mapper->validateBufferSize(imported, info, stride);
    assert(valid.isOk() && valid == am::Error::NONE);
    bool called=false;
    auto transport=mapper->getTransportSize(imported, [&](am::Error error,uint32_t fds,uint32_t ints) {
        assert(error==am::Error::NONE && fds==static_cast<unsigned>(handle->numFds) && ints<=static_cast<unsigned>(handle->numInts));
        std::printf("NATIVE_MAPPER_IMPORT_PASS width=%u height=%u format=%d fds=%u ints=%u\n",w,h,format,fds,ints);called=true;
    });
    assert(transport.isOk() && called);
    auto freed=mapper->freeBuffer(imported);
    assert(freed.isOk() && freed==am::Error::NONE);
    return usage;
}

extern "C" void quest2_native_yuv_layout_check(const native_handle_t *handle,unsigned w,unsigned h,int format,uint64_t usage) {
    namespace qe=vendor::qti::hardware::display::mapperextensions::V1_0;
    auto mapper=native_mapper();auto cast=qm::IQtiMapper::castFrom(mapper,true);assert(cast.isOk());
    sp<qm::IQtiMapper> qti=cast;assert(qti);
    sp<qe::IQtiMapperExtensions> extension;
    auto ext=qti->getMapperExtensions([&](am::Error error,const sp<qe::IQtiMapperExtensions> &value){assert(error==am::Error::NONE && value);extension=value;});
    assert(ext.isOk() && extension);
    void *imported=nullptr;
    auto result=mapper->importBuffer(handle,[&](am::Error error,void *buffer){assert(error==am::Error::NONE && buffer);imported=buffer;});
    assert(result.isOk() && imported);
    bool got_usage=false;
    auto flags=extension->getUsageFlags(imported,[&](qe::Error error,uint64_t v){assert(error==qe::Error::NONE);usage=v;got_usage=true;});assert(flags.isOk() && got_usage);
    bool called=false;
    auto layout=extension->getFormatLayout(format,usage,0,w,h,[&](qe::Error error,uint64_t size,const auto &planes){
        std::printf("QTI_YUV_LAYOUT format=0x%x error=%d size=%llu planes=%zu\n",format,static_cast<int>(error),static_cast<unsigned long long>(size),planes.size());
        for(size_t i=0;i<planes.size();i++){
            const auto &v=planes[i];
            std::printf("QTI_YUV_PLANE index=%zu component=%u offset=%u step=%d stride=%d scanlines=%d size=%u subsample=%u,%u\n",i,v.component,v.offset,v.pixel_increment,v.stride_bytes,v.scanlines,v.size,v.h_subsampling,v.v_subsampling);
        }
        called=true;
    });assert(layout.isOk() && called);
    am::IMapper::Rect rect{0,0,static_cast<int>(w),static_cast<int>(h)};
    bool locked=false;called=false;
    auto lock=mapper->lockYCbCr(imported,0x33,rect,{},[&](am::Error error,const am::YCbCrLayout &v){
        std::printf("QTI_YUV_LOCK format=0x%x error=%d y_stride=%u c_stride=%u chroma_step=%u cb_offset=%lld cr_offset=%lld\n",format,static_cast<int>(error),v.yStride,v.cStride,v.chromaStep,
            static_cast<long long>(reinterpret_cast<uintptr_t>(v.cb)-reinterpret_cast<uintptr_t>(v.y)),
            static_cast<long long>(reinterpret_cast<uintptr_t>(v.cr)-reinterpret_cast<uintptr_t>(v.y)));
        called=true;locked=error==am::Error::NONE;
    });assert(lock.isOk() && called);
    if(locked){
        auto unlock=mapper->unlock(imported,[&](am::Error error,const android::hardware::hidl_handle &fence){
            assert(error==am::Error::NONE);
            const native_handle_t *raw=fence.getNativeHandle();
            if(raw && raw->numFds) {
                assert(raw->numFds==1 && raw->numInts==0);
                struct pollfd pollfd{raw->data[0],POLLIN,0};int ready;
                do {ready=poll(&pollfd,1,5000);} while(ready<0 && errno==EINTR);
                assert(ready==1 && (pollfd.revents&POLLIN) && !(pollfd.revents&(POLLERR|POLLNVAL)));
                // HIDL owns the borrowed fd; wait in the callback without closing it.
            }
        });assert(unlock.isOk());
    }
    auto freed=mapper->freeBuffer(imported);assert(freed.isOk() && freed==am::Error::NONE);
}

extern "C" uint64_t quest2_native_yuv_geometry(const native_handle_t *handle,unsigned w,unsigned h,int format,
                                           uint64_t *capacity,uint32_t *y_stride,uint32_t *uv_stride,uint32_t *uv_offset) {
    assert(format==0x36 || format==0x7fa30c0a || format==0x7fa30c04);
    unsigned bpp=format==0x7fa30c04?1:2;
    namespace qe=vendor::qti::hardware::display::mapperextensions::V1_0;
    auto mapper=native_mapper();auto cast=qm::IQtiMapper::castFrom(mapper,true);assert(cast.isOk());
    sp<qm::IQtiMapper> qti=cast;assert(qti);
    sp<qe::IQtiMapperExtensions> extension;
    auto ext=qti->getMapperExtensions([&](am::Error error,const sp<qe::IQtiMapperExtensions> &v){assert(error==am::Error::NONE && v);extension=v;});assert(ext.isOk() && extension);
    void *imported=nullptr;auto im=mapper->importBuffer(handle,[&](am::Error error,void *v){assert(error==am::Error::NONE && v);imported=v;});assert(im.isOk() && imported);
    bool called=false;auto fmt=extension->getFormat(imported,[&](qe::Error error,int32_t actual){assert(error==qe::Error::NONE && actual==format);called=true;});assert(fmt.isOk() && called);
    uint64_t usage=0;called=false;
    auto flags=extension->getUsageFlags(imported,[&](qe::Error error,uint64_t v){assert(error==qe::Error::NONE);usage=v;called=true;});assert(flags.isOk() && called);
    called=false;
    auto result=extension->getFormatLayout(format,usage,0,w,h,[&](qe::Error error,uint64_t size,const auto &planes){
        assert(error==qe::Error::NONE && planes.size()==2);
        const auto &y=planes[0];const auto &uv=planes[1];
        assert(y.component==1 && uv.component==6 && y.offset==0);
        assert(y.stride_bytes>=static_cast<int64_t>(w)*bpp && uv.stride_bytes>=static_cast<int64_t>(w)*bpp);
        assert(y.scanlines>=static_cast<int>(h) && static_cast<uint64_t>(y.stride_bytes)*h<=uv.offset);
        assert(static_cast<uint64_t>(uv.offset)+static_cast<uint64_t>(uv.stride_bytes)*(h/2)<=size);
        assert(uv.pixel_increment==static_cast<int>(bpp*2) && y.h_subsampling==0 && y.v_subsampling==0 && uv.h_subsampling==1 && uv.v_subsampling==1);
        *capacity=size;*y_stride=y.stride_bytes;*uv_stride=uv.stride_bytes;*uv_offset=uv.offset;called=true;
    });assert(result.isOk() && called);
    auto freed=mapper->freeBuffer(imported);assert(freed.isOk() && freed==am::Error::NONE);
    return usage;
}
