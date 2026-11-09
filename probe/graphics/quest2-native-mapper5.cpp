/* Stable-C mapper5 candidate using gralloc1 directly; no mapper3/4 backend.
 * Non-protected RGB (linear/UBWC) and linear YV12/NV12/P010 with shared QTI color metadata.
 * Full metadata contract and default framework integration remain incomplete. */
#include <android/hardware/graphics/mapper/utils/IMapperProvider.h>
#include <android/hardware/graphics/mapper/utils/IMapperMetadataTypes.h>
#include <aidl/android/hardware/graphics/common/PlaneLayoutComponentType.h>
#include <mutex>
#include <unordered_map>
#include <array>
#include <poll.h>
#include <cerrno>
#include <cstring>
#include "quest2-gralloc-backend.h"
#include "quest2-color-metadata.h"
#include "quest2-gralloc-metadata.h"
#include <memory>
#include <linux/dma-buf.h>
#include <sys/ioctl.h>
#include <atomic>
namespace gc=aidl::android::hardware::graphics::common;
namespace mt=android::hardware::graphics::mapper;
using Type=gc::StandardMetadataType;
static constexpr const char *metadata_name="android.hardware.graphics.common.StandardMetadataType";
class QuestMapper5 final:public vendor::mapper::IMapperV5Impl {
    QuestGralloc backend;
    struct State {unsigned locks=0,pending=0;std::unique_ptr<QuestMetadata> metadata;};
    std::mutex mutex;
    std::unordered_map<buffer_handle_t,State> buffers;
    static AIMapper_Error error(int e) {
        if(!e)return AIMAPPER_ERROR_NONE;
        if(e==GRALLOC1_ERROR_BAD_HANDLE)return AIMAPPER_ERROR_BAD_BUFFER;
        if(e==GRALLOC1_ERROR_BAD_VALUE)return AIMAPPER_ERROR_BAD_VALUE;
        if(e==GRALLOC1_ERROR_UNSUPPORTED)return AIMAPPER_ERROR_UNSUPPORTED;
        return AIMAPPER_ERROR_NO_RESOURCES;
    }
    static uint32_t fourcc(int32_t f) {
        const char *s=f==0x32315659?"YV12":f==0x7fa30c04?"NV12":(f==0x36 || f==0x7fa30c0a)?"P010":f==22?"AB4H":f==43?"AB30":f==2?"XB24":f==5?"AR24":"AB24";
        return uint32_t(s[0])|(uint32_t(s[1])<<8)|(uint32_t(s[2])<<16)|(uint32_t(s[3])<<24);
    }
public:
    AIMapper_Error importBuffer(const native_handle_t *raw,buffer_handle_t *out) override {
        *out=nullptr;if(!backend.device)return AIMAPPER_ERROR_NO_RESOURCES;
        if(raw->version!=sizeof(native_handle_t) || raw->numFds!=2 || raw->numInts!=26)return AIMAPPER_ERROR_BAD_BUFFER;
        auto *copy=native_handle_clone(raw);if(!copy)return AIMAPPER_ERROR_NO_RESOURCES;
        int e=backend.RETAIN(backend.device,copy);
        if(e){native_handle_close(copy);native_handle_delete(copy);return error(e);}
        QuestGeometry g;
        if(!backend.geometry(copy,g)){backend.release(copy);return AIMAPPER_ERROR_UNSUPPORTED;}
        auto metadata=std::make_unique<QuestMetadata>(copy);
        if(!metadata->get()){backend.release(copy);return AIMAPPER_ERROR_BAD_BUFFER;}
        if(g.yuv && metadata->get()->interlaced){backend.release(copy);return AIMAPPER_ERROR_UNSUPPORTED;}
        {std::lock_guard<std::mutex> guard(mutex);buffers.emplace(copy,State{0,0,std::move(metadata)});}
        *out=copy;return AIMAPPER_ERROR_NONE;
    }
    AIMapper_Error freeBuffer(buffer_handle_t b) override {
        std::lock_guard<std::mutex> guard(mutex);
        auto it=buffers.find(b);if(it==buffers.end())return AIMAPPER_ERROR_BAD_BUFFER;
        if(it->second.locks || it->second.pending)return AIMAPPER_ERROR_BAD_VALUE;
        int e=backend.release(b);if(!e)buffers.erase(it);return error(e);
    }
    AIMapper_Error getTransportSize(buffer_handle_t b,uint32_t *fds,uint32_t *ints) override {
        std::lock_guard<std::mutex> guard(mutex);
        if(!buffers.count(b))return AIMAPPER_ERROR_BAD_BUFFER;
        *fds=b->numFds;*ints=b->numInts;return AIMAPPER_ERROR_NONE;
    }
    AIMapper_Error lock(buffer_handle_t b,uint64_t usage,ARect region,int fence,void **out) override {
        android::base::unique_fd owned(fence);*out=nullptr;
        std::unique_lock<std::mutex> guard(mutex);
        auto it=buffers.find(b);if(it==buffers.end())return AIMAPPER_ERROR_BAD_BUFFER;
        if(fence < -1)return AIMAPPER_ERROR_BAD_VALUE;
        const uint64_t read=usage&0xf,write=usage&0xf0;
        if(!usage || (usage&~0xffULL) || (read && read!=2 && read!=3) ||
           (write && write!=0x20 && write!=0x30))return AIMAPPER_ERROR_BAD_VALUE;
        QuestGeometry g;if(!backend.geometry(b,g))return AIMAPPER_ERROR_BAD_BUFFER;
        if(g.ubwc)return AIMAPPER_ERROR_UNSUPPORTED;
        if(!region.left && !region.top && !region.right && !region.bottom)
            region={0,0,static_cast<int32_t>(g.width),static_cast<int32_t>(g.height)};
        if(region.left<0 || region.top<0 || region.right<=region.left || region.bottom<=region.top ||
           uint32_t(region.right)>g.width || uint32_t(region.bottom)>g.height)return AIMAPPER_ERROR_BAD_VALUE;
        if(fence>=0) {
            // Keep the handle alive, but never block metadata or unrelated buffers on a GPU fence.
            it->second.pending++;guard.unlock();
            pollfd fd{fence,POLLIN,0};int ready;
            do {ready=poll(&fd,1,-1);}while(ready<0 && errno==EINTR);
            guard.lock();it=buffers.find(b);it->second.pending--;
            if(ready!=1 || !(fd.revents&POLLIN) || (fd.revents&(POLLERR|POLLNVAL)))return AIMAPPER_ERROR_BAD_VALUE;
        }
        gralloc1_rect_t rect{region.left,region.top,region.right-region.left,region.bottom-region.top};
        int e=backend.LOCK(backend.device,b,QuestGralloc::producer(usage),QuestGralloc::consumer(usage),&rect,out,-1);
        if(!e)it->second.locks++;
        return error(e);
    }
    AIMapper_Error unlock(buffer_handle_t b,int *fence) override {
        *fence=-1;std::lock_guard<std::mutex> guard(mutex);
        auto it=buffers.find(b);if(it==buffers.end())return AIMAPPER_ERROR_BAD_BUFFER;
        if(!it->second.locks)return AIMAPPER_ERROR_BAD_BUFFER;
        int e=backend.UNLOCK(backend.device,b,fence);if(!e)it->second.locks--;
        return error(e);
    }
    AIMapper_Error syncLockedBuffer(buffer_handle_t b,uint64_t flags) {
        std::unique_lock<std::mutex> guard(mutex);
        auto it=buffers.find(b);
        if(it==buffers.end() || !it->second.locks)return AIMAPPER_ERROR_BAD_BUFFER;
        // The matched two-fd handle carries pixel DMA-BUF first, coherent shared memory second.
        // Use the same END/START RW operations as the donor's clean/invalidate paths.
        it->second.pending++;guard.unlock();
        std::atomic_thread_fence(std::memory_order_seq_cst);
        dma_buf_sync sync{flags};int result;
        do {result=ioctl(b->data[0],DMA_BUF_IOCTL_SYNC,&sync);}while(result<0 && (errno==EINTR || errno==EAGAIN));
        std::atomic_thread_fence(std::memory_order_seq_cst);
        // DMA-BUF CPU access can wait for hardware; keep other buffers and metadata accessible.
        guard.lock();buffers.at(b).pending--;
        return result?AIMAPPER_ERROR_NO_RESOURCES:AIMAPPER_ERROR_NONE;
    }
    AIMapper_Error flushLockedBuffer(buffer_handle_t b) override {
        return syncLockedBuffer(b,DMA_BUF_SYNC_END|DMA_BUF_SYNC_RW);
    }
    AIMapper_Error rereadLockedBuffer(buffer_handle_t b) override {
        return syncLockedBuffer(b,DMA_BUF_SYNC_START|DMA_BUF_SYNC_RW);
    }
    int32_t getStandardMetadata(buffer_handle_t b,int64_t type,void *dest,size_t size) override {
        std::lock_guard<std::mutex> guard(mutex);
        return getStandardMetadataLocked(b,type,dest,size);
    }
    int32_t getStandardMetadataLocked(buffer_handle_t b,int64_t type,void *dest,size_t size) {
        auto it=buffers.find(b);if(it==buffers.end())return -AIMAPPER_ERROR_BAD_BUFFER;
        auto *shared=it->second.metadata->get();
        if(!dest && size)return -AIMAPPER_ERROR_BAD_VALUE;
        QuestGeometry g;if(!backend.geometry(b,g))return -AIMAPPER_ERROR_UNSUPPORTED;
#define ENCODE(kind,item) case Type::kind:return mt::StandardMetadata<Type::kind>::value::encode(item,dest,size)
        switch(static_cast<Type>(type)) {
            ENCODE(BUFFER_ID,g.id);
            case Type::NAME:
                return mt::StandardMetadata<Type::NAME>::value::encode(
                    std::string_view(shared->name,strnlen(shared->name,sizeof(shared->name))),dest,size);
            ENCODE(BLEND_MODE,static_cast<gc::BlendMode>(shared->blendMode));
            case Type::INTERLACED:case Type::CHROMA_SITING: {
                if(static_cast<Type>(type)==Type::INTERLACED && shared->interlaced)
                    return -AIMAPPER_ERROR_UNSUPPORTED;
                gc::ExtendableType value;value.value=0;
                if(static_cast<Type>(type)==Type::INTERLACED) {
                    value.name="android.hardware.graphics.common.Interlaced";
                    return mt::StandardMetadata<Type::INTERLACED>::value::encode(value,dest,size);
                }
                value.name="android.hardware.graphics.common.ChromaSiting";
                return mt::StandardMetadata<Type::CHROMA_SITING>::value::encode(value,dest,size);
            }
            case Type::CROP: {
                gc::Rect r;r.right=g.width;r.bottom=g.height;
                if(shared->isStandardMetadataSet[16]) {
                    r.left=shared->crop.left;r.top=shared->crop.top;
                    r.right=shared->crop.right;r.bottom=shared->crop.bottom;
                }
                if(r.left<0 || r.top<0 || r.right<r.left || r.bottom<r.top ||
                   uint32_t(r.right)>g.width || uint32_t(r.bottom)>g.height)return -AIMAPPER_ERROR_BAD_BUFFER;
                std::vector<gc::Rect> crops{r};
                if(g.yuv) {
                    gc::Rect uv;uv.left=r.left/2;uv.top=r.top/2;
                    uv.right=(r.right+1)/2;uv.bottom=(r.bottom+1)/2;crops.push_back(uv);
                    if(g.planar_yv12)crops.push_back(uv);
                }
                return mt::StandardMetadata<Type::CROP>::value::encode(crops,dest,size);
            }
            ENCODE(WIDTH,g.width);ENCODE(HEIGHT,g.height);ENCODE(LAYER_COUNT,g.layers);
            ENCODE(PIXEL_FORMAT_REQUESTED,static_cast<gc::PixelFormat>(g.format));
            ENCODE(PIXEL_FORMAT_FOURCC,fourcc(g.format));
            ENCODE(PIXEL_FORMAT_MODIFIER,g.ubwc?0x0500000000000001ULL:0ULL); // DRM_FORMAT_MOD_QCOM_COMPRESSED
            ENCODE(USAGE,static_cast<gc::BufferUsage>(g.usage));ENCODE(ALLOCATION_SIZE,g.size);
            ENCODE(PROTECTED_CONTENT,0);ENCODE(STRIDE,g.stride);
            case Type::DATASPACE:case Type::SMPTE2086:case Type::CTA861_3: {
                ColorMetaData color{};questcolor::read(b,color);
                if(static_cast<Type>(type)==Type::DATASPACE)
                    return mt::StandardMetadata<Type::DATASPACE>::value::encode(questcolor::dataspace(color),dest,size);
                if(static_cast<Type>(type)==Type::SMPTE2086)
                    return mt::StandardMetadata<Type::SMPTE2086>::value::encode(questcolor::mastering(color),dest,size);
                return mt::StandardMetadata<Type::CTA861_3>::value::encode(questcolor::light(color),dest,size);
            }
            case Type::COMPRESSION: {
                gc::ExtendableType value;value.name=g.ubwc?"QTI":"android.hardware.graphics.common.Compression";
                value.value=g.ubwc?COMPRESSION_QTI_UBWC:0;
                return mt::StandardMetadata<Type::COMPRESSION>::value::encode(value,dest,size);
            }
            case Type::PLANE_LAYOUTS: {
                if(g.yuv) {
                    std::vector<gc::PlaneLayout> planes;
                    for(unsigned i=0;i<(g.planar_yv12?3u:2u);i++) {
                        const auto &source=i==2?g.chroma2:i?g.chroma:g.plane;
                        gc::PlaneLayout plane;
                        for(unsigned c=0;c<((i && !g.planar_yv12)?2u:1u);c++) {
                            gc::PlaneLayoutComponent component;
                            component.type.name="android.hardware.graphics.common.PlaneLayoutComponentType";
                            component.type.value=static_cast<int64_t>(!i?gc::PlaneLayoutComponentType::Y:
                                g.planar_yv12?(i==1?gc::PlaneLayoutComponentType::CR:gc::PlaneLayoutComponentType::CB):
                                c?gc::PlaneLayoutComponentType::CR:gc::PlaneLayoutComponentType::CB);
                            component.offsetInBits=c*g.sample_bytes*8+(g.sample_bytes==2?6:0);
                            component.sizeInBits=g.sample_bytes==2?10:8;plane.components.push_back(component);
                        }
                        plane.offsetInBytes=source.offset;plane.sampleIncrementInBits=source.step*8;
                        plane.strideInBytes=source.stride_bytes;plane.widthInSamples=g.width/(i?2:1);
                        plane.heightInSamples=g.height/(i?2:1);plane.totalSizeInBytes=source.size;
                        plane.horizontalSubsampling=plane.verticalSubsampling=i?2:1;
                        planes.push_back(std::move(plane));
                    }
                    return mt::StandardMetadata<Type::PLANE_LAYOUTS>::value::encode(planes,dest,size);
                }
                gc::PlaneLayout plane;int offset=0;
                const gc::PlaneLayoutComponentType types[]={gc::PlaneLayoutComponentType::R,gc::PlaneLayoutComponentType::G,
                    gc::PlaneLayoutComponentType::B,gc::PlaneLayoutComponentType::A};
                for(unsigned i=0;i<(g.format==2?3u:4u);i++) {
                    gc::PlaneLayoutComponent c;c.type.name="android.hardware.graphics.common.PlaneLayoutComponentType";
                    c.type.value=static_cast<int64_t>(types[g.format==5 && i<3?2-i:i]);c.offsetInBits=offset;
                    c.sizeInBits=g.format==22?16:g.format==43?(i==3?2:10):8;
                    offset+=c.sizeInBits;
                    if(g.ubwc)c.offsetInBits=c.sizeInBits=-1;
                    plane.components.push_back(c);
                }
                // Standard planes describe payload bytes; KMS UBWC uses the allocation base instead.
                plane.offsetInBytes=g.plane.offset;plane.sampleIncrementInBits=g.ubwc?0:g.plane.step*8;
                plane.strideInBytes=g.ubwc?0:g.plane.stride_bytes;
                plane.widthInSamples=g.width;plane.heightInSamples=g.height;
                plane.totalSizeInBytes=g.ubwc?g.size-g.plane.offset:g.plane.size;
                plane.horizontalSubsampling=1;plane.verticalSubsampling=1;
                return mt::StandardMetadata<Type::PLANE_LAYOUTS>::value::encode({plane},dest,size);
            }
            default:return -AIMAPPER_ERROR_UNSUPPORTED;
        }
#undef ENCODE
    }
    int32_t getMetadata(buffer_handle_t b,AIMapper_MetadataType t,void *dest,size_t size) override {
        if(std::strcmp(t.name,metadata_name))return -AIMAPPER_ERROR_UNSUPPORTED;
        return getStandardMetadata(b,t.value,dest,size);
    }
    AIMapper_Error setStandardMetadata(buffer_handle_t b,int64_t type,const void *data,size_t size) override {
        std::lock_guard<std::mutex> guard(mutex);
        if(!buffers.count(b))return AIMAPPER_ERROR_BAD_BUFFER;
        switch(static_cast<Type>(type)) {
            case Type::BUFFER_ID:case Type::NAME:case Type::WIDTH:case Type::HEIGHT:
            case Type::LAYER_COUNT:case Type::PIXEL_FORMAT_REQUESTED:case Type::USAGE:
                return AIMAPPER_ERROR_BAD_VALUE;
            default:break;
        }
        if(!data && size)return AIMAPPER_ERROR_UNSUPPORTED;
        if(static_cast<Type>(type)==Type::BLEND_MODE) {
            auto v=mt::StandardMetadata<Type::BLEND_MODE>::value::decode(data,size);
            if(!v || int32_t(*v)<0 || int32_t(*v)>3)return AIMAPPER_ERROR_UNSUPPORTED;
            auto *shared=buffers.at(b).metadata->get();
            shared->blendMode=int32_t(*v);shared->isStandardMetadataSet[18]=true;
            return AIMAPPER_ERROR_NONE;
        }
        ColorMetaData color{};questcolor::read(b,color);
        bool valid=false,metadata_set=true;
        switch(static_cast<Type>(type)) {
            case Type::DATASPACE: {
                auto v=mt::StandardMetadata<Type::DATASPACE>::value::decode(data,size);
                valid=v && questcolor::setDataspace(*v,color);break;
            }
            case Type::SMPTE2086: {
                auto v=mt::StandardMetadata<Type::SMPTE2086>::value::decode(data,size);
                valid=v && questcolor::setMastering(*v,color);
                if(valid)metadata_set=v->has_value();break;
            }
            case Type::CTA861_3: {
                auto v=mt::StandardMetadata<Type::CTA861_3>::value::decode(data,size);
                valid=v && questcolor::setLight(*v,color);
                if(valid)metadata_set=v->has_value();break;
            }
            default:return AIMAPPER_ERROR_UNSUPPORTED;
        }
        if(!valid)return AIMAPPER_ERROR_UNSUPPORTED;
        if(!questcolor::write(b,color))return AIMAPPER_ERROR_NO_RESOURCES;
        // Donor COLOR_METADATA sets its vendor bit; standard setters own their individual state.
        buffers.at(b).metadata->get()->isStandardMetadataSet[type]=metadata_set;
        return AIMAPPER_ERROR_NONE;
    }
    AIMapper_Error setMetadata(buffer_handle_t b,AIMapper_MetadataType t,const void *data,size_t size) override {
        if(std::strcmp(t.name,metadata_name))return AIMAPPER_ERROR_UNSUPPORTED;
        return setStandardMetadata(b,t.value,data,size);
    }
    AIMapper_Error listSupportedMetadataTypes(const AIMapper_MetadataTypeDescription **out,size_t *count) override {
        static const auto descriptions=[]{
            std::array<AIMapper_MetadataTypeDescription,21> result{};
            const int64_t ids[]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,23};
            for(unsigned i=0;i<result.size();i++){
                result[i].metadataType={metadata_name,ids[i]};result[i].isGettable=true;
                result[i].isSettable=ids[i]==17 || ids[i]==18 || ids[i]==19 || ids[i]==20;
            }
            return result;
        }();
        *out=descriptions.data();*count=descriptions.size();return AIMAPPER_ERROR_NONE;
    }
    using Dump=std::vector<std::pair<AIMapper_MetadataType,std::vector<uint8_t>>>;
    AIMapper_Error snapshotLocked(buffer_handle_t b,Dump &out) {
        if(!buffers.count(b))return AIMAPPER_ERROR_BAD_BUFFER;
        const AIMapper_MetadataTypeDescription *types=nullptr;size_t count=0;
        listSupportedMetadataTypes(&types,&count);
        for(size_t i=0;i<count;i++) {
            const auto type=types[i].metadataType;
            int32_t size=getStandardMetadataLocked(b,type.value,nullptr,0);
            if(size<0)return static_cast<AIMapper_Error>(-size);
            std::vector<uint8_t> value(size);
            if(getStandardMetadataLocked(b,type.value,value.data(),value.size())!=size)
                return AIMAPPER_ERROR_NO_RESOURCES;
            out.emplace_back(type,std::move(value));
        }
        return AIMAPPER_ERROR_NONE;
    }
    static void emitDump(const Dump &dump,AIMapper_DumpBufferCallback callback,void *context) {
        const uint8_t empty=0;
        for(const auto &field:dump)
            callback(context,field.first,field.second.empty()?&empty:field.second.data(),field.second.size());
    }
    AIMapper_Error dumpBuffer(buffer_handle_t b,AIMapper_DumpBufferCallback callback,void *context) override {
        Dump dump;
        {
            std::lock_guard<std::mutex> guard(mutex);
            auto result=snapshotLocked(b,dump);if(result!=AIMAPPER_ERROR_NONE)return result;
        }
        // Client callbacks may reenter the mapper or release an imported handle.
        emitDump(dump,callback,context);return AIMAPPER_ERROR_NONE;
    }
    AIMapper_Error dumpAllBuffers(AIMapper_BeginDumpBufferCallback begin,AIMapper_DumpBufferCallback callback,void *context) override {
        std::vector<Dump> dumps;
        {
            std::lock_guard<std::mutex> guard(mutex);
            for(const auto &buffer:buffers) {
                dumps.emplace_back();
                auto result=snapshotLocked(buffer.first,dumps.back());if(result!=AIMAPPER_ERROR_NONE)return result;
            }
        }
        for(const auto &dump:dumps){begin(context);emitDump(dump,callback,context);}
        return AIMAPPER_ERROR_NONE;
    }
    AIMapper_Error getReservedRegion(buffer_handle_t b,void **out,uint64_t *size) override {
        *out=nullptr;*size=0;std::lock_guard<std::mutex> guard(mutex);
        return buffers.count(b)?AIMAPPER_ERROR_NONE:AIMAPPER_ERROR_BAD_BUFFER;
    }
};
static vendor::mapper::IMapperProvider<QuestMapper5> provider;
extern "C" AIMapper_Error AIMapper_loadIMapper(AIMapper **out) {return provider.load(out);}
extern "C" {uint32_t ANDROID_HAL_MAPPER_VERSION=5;}
