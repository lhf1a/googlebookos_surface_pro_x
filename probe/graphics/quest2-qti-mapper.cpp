/* Experimental process-local QTI metadata adapter. Load only as a dependency
 * of the isolated donor (or probe preload). No registered service/system HAL changes.
 * Initial scope: unprotected linear RGBA8888 buffers. Other operations fail
 * explicitly; this is not a complete Android mapper implementation. */
#include <vendor/qti/hardware/display/mapper/4.0/IQtiMapper.h>
#include <vendor/qti/hardware/display/mapperextensions/1.1/IQtiMapperExtensions.h>
#include <cstdio>
#include "quest2-mapper5.h"
namespace am=android::hardware::graphics::mapper::V4_0;
namespace qm=vendor::qti::hardware::display::mapper::V4_0;
namespace qe=vendor::qti::hardware::display::mapperextensions::V1_0;
namespace qe1=vendor::qti::hardware::display::mapperextensions::V1_1;
using android::hardware::Return;
using android::hardware::Void;
using android::hardware::hidl_handle;
using android::hardware::hidl_vec;
using android::sp;

namespace sq2 {
static bool info(void *buffer,BufferInfo &b,const char *operation) {
    bool ok=color_info(static_cast<const native_handle_t*>(buffer),b)&&b.format==1&&
        b.fourcc==0x34324241u && b.sample_bits==32 && !(b.usage&(1ull<<14)) &&
        b.stride/4<=INT32_MAX && b.width<=INT32_MAX && b.height<=INT32_MAX &&
        b.stride%4==0 && b.offset%4==0 && b.width<=b.stride/4 && b.height<=b.plane_size/b.stride;
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
    GETTER(getHeight, b.height, int32_t)
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
        cb(ok?qe::Error::NONE:qe::Error::BAD_BUFFER,ok?b.stride/4:0,ok?b.height:0);return Void();
    }
    Return<void> getRgbDataAddress(void*,getRgbDataAddress_cb cb) override {cb(qe::Error::UNSUPPORTED,nullptr);return Void();}
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
    Return<void> lock(void*,uint64_t,const Rect&,const hidl_handle&,lock_cb cb) override {cb(am::Error::UNSUPPORTED,nullptr);return Void();}
    Return<void> unlock(void*,unlock_cb cb) override {cb(am::Error::UNSUPPORTED,{});return Void();}
    Return<void> flushLockedBuffer(void*,flushLockedBuffer_cb cb) override {cb(am::Error::UNSUPPORTED,{});return Void();}
    Return<am::Error> rereadLockedBuffer(void*) override {return am::Error::UNSUPPORTED;}
    Return<void> isSupported(const BufferDescriptorInfo&,isSupported_cb cb) override {cb(am::Error::UNSUPPORTED,false);return Void();}
    Return<void> get(void*,const MetadataType&,get_cb cb) override {cb(am::Error::UNSUPPORTED,{});return Void();}
    Return<am::Error> set(void*,const MetadataType&,const hidl_vec<uint8_t>&) override {return am::Error::UNSUPPORTED;}
    Return<void> getFromBufferDescriptorInfo(const BufferDescriptorInfo&,const MetadataType&,getFromBufferDescriptorInfo_cb cb) override {cb(am::Error::UNSUPPORTED,{});return Void();}
    Return<void> listSupportedMetadataTypes(listSupportedMetadataTypes_cb cb) override {cb(am::Error::UNSUPPORTED,{});return Void();}
    Return<void> dumpBuffer(void*,dumpBuffer_cb cb) override {cb(am::Error::UNSUPPORTED,{});return Void();}
    Return<void> dumpBuffers(dumpBuffers_cb cb) override {cb(am::Error::UNSUPPORTED,{});return Void();}
    Return<void> getReservedRegion(void*,getReservedRegion_cb cb) override {cb(am::Error::UNSUPPORTED,nullptr,0);return Void();}
    Return<void> getMapperExtensions(getMapperExtensions_cb cb) override {
        std::fprintf(stderr,"QTI_BRIDGE extensions requested\n");cb(am::Error::NONE,extension());return Void();
    }
};
static sp<Mapper> mapper() {static sp<Mapper> instance=new Mapper;return instance;}
}

// Only these three entrypoints are interposed in the isolated probe process.
sp<am::IMapper> am::IMapper::getService(const std::string&,bool) {
    std::fprintf(stderr,"QTI_BRIDGE local mapper4 requested\n");return sq2::mapper();
}
Return<sp<qm::IQtiMapper>> qm::IQtiMapper::castFrom(const sp<am::IMapper> &parent,bool) {
    if(parent.get()!=sq2::mapper().get())return sp<qm::IQtiMapper>();
    return sp<qm::IQtiMapper>(sq2::mapper());
}
Return<sp<qe1::IQtiMapperExtensions>> qe1::IQtiMapperExtensions::castFrom(const sp<qe::IQtiMapperExtensions> &parent,bool) {
    if(parent.get()!=sq2::extension().get())return sp<qe1::IQtiMapperExtensions>();
    return sp<qe1::IQtiMapperExtensions>(sq2::extension());
}
