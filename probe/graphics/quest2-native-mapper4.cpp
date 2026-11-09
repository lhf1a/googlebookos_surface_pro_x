/* Mapper4 import/metadata adapter over the genuine QTI mapper3.
 * Allocation service and mutable/HDR metadata are not implemented here.
 * No private_handle offsets and no replacement pixel storage. */
#include <android/hardware/graphics/mapper/4.0/IMapper.h>
#include <vendor/qti/hardware/display/mapper/3.0/IQtiMapper.h>
#include <gralloctypes/Gralloc4.h>
#include <mutex>
#include <unordered_map>
#include <cstring>
#include <cstdio>
namespace m3=android::hardware::graphics::mapper::V3_0;
namespace m4=android::hardware::graphics::mapper::V4_0;
namespace q3=vendor::qti::hardware::display::mapper::V3_0;
namespace qe=vendor::qti::hardware::display::mapperextensions::V1_0;
namespace common=aidl::android::hardware::graphics::common;
namespace g4=android::gralloc4;
using android::sp;
using android::hardware::Return;
using android::hardware::hidl_vec;
using android::hardware::hidl_handle;
using Error=m4::Error;
static Error converted(m3::Error e){return static_cast<Error>(e);}
static bool rgb(int32_t format){return format==1 || format==43 || format==22;}
struct Geometry {
    int32_t width=0,height=0,aligned_width=0,aligned_height=0,format=0;
    uint64_t usage=0,size=0,id=0,layers=0,offset=0;
    qe::PlaneLayout plane{};
};
class Mapper4 final:public m4::IMapper {
    sp<m3::IMapper> mapper;
    sp<qe::IQtiMapperExtensions> ext;
    std::mutex mutex;
    std::unordered_map<void *,unsigned> imports;
    bool geometry(void *b,Geometry &v) {
        bool ok=true;
#define GET(method,field) {auto r=ext->method(b,[&](qe::Error e,auto x){ok &= e==qe::Error::NONE;v.field=x;});ok &= r.isOk();}
        GET(getFormat,format);GET(getUnalignedWidth,width);GET(getUnalignedHeight,height);
        GET(getWidth,aligned_width);GET(getHeight,aligned_height);GET(getSize,size);
        GET(getUsageFlags,usage);GET(getLayerCount,layers);GET(getOffset,offset);GET(getId,id);
#undef GET
        if(!ok || !rgb(v.format) || v.width<=0 || v.height<=0 || v.layers!=1 ||
           (v.usage & 0x4000) || v.offset || !v.size)return false;
        bool attributes=false;
        auto a=ext->calculateBufferAttributes(v.width,v.height,v.format,v.usage,
            [&](qe::Error e,int32_t w,int32_t h,bool ubwc){
                attributes=e==qe::Error::NONE && !ubwc && w==v.aligned_width && h==v.aligned_height;
            });
        bool layout=false;
        auto l=ext->getFormatLayout(v.format,v.usage,0,v.width,v.height,
            [&](qe::Error e,uint64_t size,const auto &planes){
                if(e==qe::Error::NONE && size==v.size && planes.size()==1){v.plane=planes[0];layout=true;}
            });
        const auto &x=v.plane;
        int bpp=v.format==22?8:4;
        return a.isOk() && attributes && l.isOk() && layout &&
            x.component==((1u<<10)|(1u<<11)|(1u<<12)|(1u<<20)) &&
            !x.offset && x.pixel_increment==bpp && x.stride==v.aligned_width &&
            x.stride_bytes>0 && x.stride_bytes/bpp==v.aligned_width &&
            x.stride_bytes%bpp==0 && x.scanlines>=v.height && x.scanlines==v.aligned_height &&
            x.h_subsampling==0 && x.v_subsampling==0 &&
            static_cast<uint64_t>(x.stride_bytes)*x.scanlines<=x.size && x.size<=v.size;
    }
    static uint32_t fourcc(int32_t f) {
        // DRM_FORMAT_ABGR8888, ABGR2101010, ABGR16161616F (little endian).
        const char *s=f==22?"AB4H":f==43?"AB30":"AB24";
        return uint32_t(s[0])|(uint32_t(s[1])<<8)|(uint32_t(s[2])<<16)|(uint32_t(s[3])<<24);
    }
    Error encode(void *b,const MetadataType &type,hidl_vec<uint8_t> &out) {
        if(!imports.count(b))return Error::BAD_BUFFER;
        if(type.name!=GRALLOC4_STANDARD_METADATA_TYPE)return Error::UNSUPPORTED;
        Geometry v;if(!geometry(b,v))return Error::UNSUPPORTED;
        int status=-1;
        switch(static_cast<common::StandardMetadataType>(type.value)) {
        case common::StandardMetadataType::BUFFER_ID:status=g4::encodeBufferId(v.id,&out);break;
        case common::StandardMetadataType::WIDTH:status=g4::encodeWidth(v.width,&out);break;
        case common::StandardMetadataType::HEIGHT:status=g4::encodeHeight(v.height,&out);break;
        case common::StandardMetadataType::LAYER_COUNT:status=g4::encodeLayerCount(v.layers,&out);break;
        case common::StandardMetadataType::PIXEL_FORMAT_REQUESTED:
            status=g4::encodePixelFormatRequested(static_cast<android::hardware::graphics::common::V1_2::PixelFormat>(v.format),&out);break;
        case common::StandardMetadataType::PIXEL_FORMAT_FOURCC:status=g4::encodePixelFormatFourCC(fourcc(v.format),&out);break;
        case common::StandardMetadataType::PIXEL_FORMAT_MODIFIER:status=g4::encodePixelFormatModifier(0,&out);break;
        case common::StandardMetadataType::USAGE:status=g4::encodeUsage(v.usage,&out);break;
        case common::StandardMetadataType::ALLOCATION_SIZE:status=g4::encodeAllocationSize(v.size,&out);break;
        case common::StandardMetadataType::PROTECTED_CONTENT:status=g4::encodeProtectedContent(0,&out);break;
        case common::StandardMetadataType::COMPRESSION:status=g4::encodeCompression(g4::Compression_None,&out);break;
        case common::StandardMetadataType::PLANE_LAYOUTS: {
            common::PlaneLayout plane;
            const common::PlaneLayoutComponentType types[]={common::PlaneLayoutComponentType::R,
                common::PlaneLayoutComponentType::G,common::PlaneLayoutComponentType::B,common::PlaneLayoutComponentType::A};
            int offset=0;
            for(unsigned i=0;i<4;i++) {
                common::PlaneLayoutComponent c;
                c.type.name=GRALLOC4_STANDARD_PLANE_LAYOUT_COMPONENT_TYPE;
                c.type.value=static_cast<int64_t>(types[i]);c.offsetInBits=offset;
                c.sizeInBits=v.format==22?16:v.format==43?(i==3?2:10):8;
                offset+=c.sizeInBits;plane.components.push_back(c);
            }
            plane.offsetInBytes=v.plane.offset;plane.sampleIncrementInBits=v.plane.pixel_increment*8;
            plane.strideInBytes=v.plane.stride_bytes;plane.widthInSamples=v.width;
            plane.heightInSamples=v.height;plane.totalSizeInBytes=v.plane.size;
            // QTI RGB uses exponent zero; mapper4 specifies factor one.
            // Logical sample dimensions exclude row/scanline allocation padding.
            plane.horizontalSubsampling=1;plane.verticalSubsampling=1;
            status=g4::encodePlaneLayouts({plane},&out);break;
        }
        default:return Error::UNSUPPORTED;
        }
        return status==0?Error::NONE:Error::NO_RESOURCES;
    }
public:
    Mapper4(const sp<m3::IMapper> &m,const sp<qe::IQtiMapperExtensions> &e):mapper(m),ext(e){}
    Return<void> createDescriptor(const BufferDescriptorInfo &,createDescriptor_cb cb) override {
        cb(Error::UNSUPPORTED,{});return {};
    }
    Return<void> importBuffer(const hidl_handle &h,importBuffer_cb cb) override {
        Error error=Error::NO_RESOURCES;void *buffer=nullptr;
        auto r=mapper->importBuffer(h,[&](m3::Error e,void *b){error=converted(e);buffer=b;});
        if(!r.isOk()){cb(Error::NO_RESOURCES,nullptr);return {};}
        if(error==Error::NONE) {
            Geometry v;
            if(!geometry(buffer,v)){
                auto freed=mapper->freeBuffer(buffer);(void)freed.isOk();
                error=Error::UNSUPPORTED;buffer=nullptr;
            } else {
                std::lock_guard<std::mutex> guard(mutex);imports[buffer]++;
            }
        }
        cb(error,buffer);return {};
    }
    Return<Error> freeBuffer(void *b) override {
        std::lock_guard<std::mutex> guard(mutex);
        auto it=imports.find(b);if(it==imports.end())return Error::BAD_BUFFER;
        auto r=mapper->freeBuffer(b);if(!r.isOk())return Error::NO_RESOURCES;
        Error e=converted(r);if(e==Error::NONE && --it->second==0)imports.erase(it);
        return e;
    }
    Return<Error> validateBufferSize(void *b,const BufferDescriptorInfo &d,uint32_t stride) override {
        std::lock_guard<std::mutex> guard(mutex);
        if(!imports.count(b))return Error::BAD_BUFFER;
        if(d.reservedSize)return Error::UNSUPPORTED;
        m3::IMapper::BufferDescriptorInfo info{d.width,d.height,d.layerCount,d.format,d.usage};
        auto r=mapper->validateBufferSize(b,info,stride);
        return r.isOk()?converted(r):Error::NO_RESOURCES;
    }
    Return<void> getTransportSize(void *b,getTransportSize_cb cb) override {
        std::lock_guard<std::mutex> guard(mutex);
        if(!imports.count(b)){cb(Error::BAD_BUFFER,0,0);return {};}
        return mapper->getTransportSize(b,[&](m3::Error e,uint32_t f,uint32_t i){cb(converted(e),f,i);});
    }
    Return<void> lock(void *b,uint64_t u,const Rect &r,const hidl_handle &f,lock_cb cb) override {
        std::lock_guard<std::mutex> guard(mutex);
        if(!imports.count(b)){cb(Error::BAD_BUFFER,nullptr);return {};}
        m3::IMapper::Rect rect{r.left,r.top,r.width,r.height};
        return mapper->lock(b,u,rect,f,[&](m3::Error e,void *data,int32_t,int32_t){cb(converted(e),data);});
    }
    Return<void> unlock(void *b,unlock_cb cb) override {
        std::lock_guard<std::mutex> guard(mutex);
        if(!imports.count(b)){cb(Error::BAD_BUFFER,{});return {};}
        return mapper->unlock(b,[&](m3::Error e,const hidl_handle &f){cb(converted(e),f);});
    }
    Return<void> flushLockedBuffer(void *,flushLockedBuffer_cb cb) override {cb(Error::UNSUPPORTED,{});return {};}
    Return<Error> rereadLockedBuffer(void *) override {return Error::UNSUPPORTED;}
    Return<void> isSupported(const BufferDescriptorInfo &,isSupported_cb cb) override {cb(Error::NONE,false);return {};}
    Return<void> get(void *b,const MetadataType &t,get_cb cb) override {
        hidl_vec<uint8_t> data;Error e;
        {std::lock_guard<std::mutex> guard(mutex);e=encode(b,t,data);}
        cb(e,data);return {};
    }
    Return<Error> set(void *b,const MetadataType &,const hidl_vec<uint8_t> &) override {
        std::lock_guard<std::mutex> guard(mutex);return imports.count(b)?Error::UNSUPPORTED:Error::BAD_BUFFER;
    }
    Return<void> getFromBufferDescriptorInfo(const BufferDescriptorInfo &,const MetadataType &,getFromBufferDescriptorInfo_cb cb) override {cb(Error::UNSUPPORTED,{});return {};}
    Return<void> listSupportedMetadataTypes(listSupportedMetadataTypes_cb cb) override {
        hidl_vec<MetadataTypeDescription> result;result.resize(12);
        const int64_t ids[]={1,3,4,5,6,7,8,9,10,11,12,15};
        for(unsigned i=0;i<12;i++){
            result[i].metadataType={GRALLOC4_STANDARD_METADATA_TYPE,ids[i]};
            result[i].description="Verified native linear RGB allocation metadata";
            result[i].isGettable=true;result[i].isSettable=false;
        }
        cb(Error::NONE,result);return {};
    }
    Return<void> dumpBuffer(void *,dumpBuffer_cb cb) override {cb(Error::UNSUPPORTED,{});return {};}
    Return<void> dumpBuffers(dumpBuffers_cb cb) override {cb(Error::UNSUPPORTED,{});return {};}
    Return<void> getReservedRegion(void *,getReservedRegion_cb cb) override {cb(Error::UNSUPPORTED,nullptr,0);return {};}
};
extern "C" m4::IMapper *HIDL_FETCH_IMapper(const char *name) {
    if(!name || std::strcmp(name,"default"))return nullptr;
    auto m=m3::IMapper::getService("default",true);if(!m || m->isRemote())return nullptr;
    auto c=q3::IQtiMapper::castFrom(m);if(!c.isOk())return nullptr;
    sp<q3::IQtiMapper> q=c;if(!q)return nullptr;
    sp<qe::IQtiMapperExtensions> ext;
    auto r=q->getMapperExtensions([&](m3::Error e,const auto &v){if(e==m3::Error::NONE)ext=v;});
    if(!r.isOk() || !ext)return nullptr;
    return new Mapper4(m,ext);
}
