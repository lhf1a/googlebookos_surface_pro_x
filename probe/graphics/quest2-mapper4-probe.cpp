/* Real HIDL allocator IPC and original mapper import; no service substitution. */
#include <android/hardware/graphics/allocator/3.0/IAllocator.h>
#include <android/hardware/graphics/mapper/3.0/IMapper.h>
#include <android/hardware/graphics/mapper/4.0/IMapper.h>
#include <gralloctypes/Gralloc4.h>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <poll.h>
#include <unistd.h>
#include <cstring>
namespace am=android::hardware::graphics::mapper::V4_0;
namespace m3=android::hardware::graphics::mapper::V3_0;
namespace g4=android::gralloc4;
namespace gc=aidl::android::hardware::graphics::common;
namespace aa=android::hardware::graphics::allocator::V3_0;
using android::hardware::hidl_handle;
static void unlock(const android::sp<am::IMapper> &mapper,void *buffer) {
    bool called=false;
    auto result=mapper->unlock(buffer,[&](am::Error error,const hidl_handle &fence){
        assert(error==am::Error::NONE);called=true;
        const native_handle_t *h=fence.getNativeHandle();
        if(h && h->numFds) {
            assert(h->numFds==1 && h->numInts==0);
            struct pollfd p{h->data[0],POLLIN,0};int r;
            do{r=poll(&p,1,5000);}while(r<0 && errno==EINTR);
            assert(r==1 && (p.revents&POLLIN) && !(p.revents&(POLLERR|POLLNVAL)));
        }
    });assert(result.isOk() && called);
}
int main() {
    setvbuf(stdout,nullptr,_IONBF,0);
    auto allocator=aa::IAllocator::tryGetService("default");
    if(!allocator){std::fprintf(stderr,"ALLOCATOR_NOT_REGISTERED\n");return 2;}
    assert(allocator->isRemote());
    bool qti=false;
    auto chain=allocator->interfaceChain([&](const auto &names){
        for(size_t i=0;i<names.size();i++){
            const auto &name=names[i];
            std::printf("ALLOCATOR_INTERFACE %s\n",name.c_str());
            if(name=="vendor.qti.hardware.display.allocator@3.0::IQtiAllocator")qti=true;
        }
    });assert(chain.isOk() && qti);
    auto debug=allocator->getDebugInfo([](const auto &info){
        std::printf("ALLOCATOR_REMOTE pid=%d client=%d\n",info.pid,getpid());
        assert(info.pid>0 && info.pid!=getpid());
    });assert(debug.isOk());
    auto mapper=am::IMapper::getService("default",true);
    assert(mapper && !mapper->isRemote());
    FILE *maps=std::fopen("/proc/self/maps","r");assert(maps);
    char line[2048];bool hal=false,original=false;
    while(std::fgets(line,sizeof(line),maps)) {
        assert(!std::strstr(line,"libquest2-native-mapper.so"));
        if(std::strstr(line,"android.hardware.graphics.mapper@4.0-impl-quest2.so"))hal=true;
        if(std::strstr(line,"libquest2-original-mapper3.so"))original=true;
    }
    std::fclose(maps);assert(hal && original);
    std::puts("STANDARD_MAPPER_HAL loaded=1 private_getService=0");

    auto descriptor_mapper=m3::IMapper::getService("default",true);assert(descriptor_mapper);
    unsigned verified=0;
    auto metadata=[&](void *buffer,const auto &type) {
        android::hardware::hidl_vec<uint8_t> data;bool called=false;
        auto r=mapper->get(buffer,type,[&](am::Error error,const auto &value){
            assert(error==am::Error::NONE && value.size());data=value;called=true;
        });assert(r.isOk() && called);return data;
    };
    for(int format:{1,43,22}) for(unsigned w:{64U,129U,1280U}) {
        unsigned pixel_bytes=format==22?8:4;
        unsigned h=w==1280?720:65;
        am::IMapper::BufferDescriptorInfo info{};
        info.width=w;info.height=h;info.layerCount=1;
        info.format=static_cast<android::hardware::graphics::common::V1_2::PixelFormat>(format);
        info.usage=0x133; // CPU_READ/WRITE_OFTEN and GPU_TEXTURE, legacy HIDL usage.
        android::hardware::hidl_vec<uint32_t> descriptor;
        m3::IMapper::BufferDescriptorInfo old{info.width,info.height,info.layerCount,info.format,info.usage};
        auto created=descriptor_mapper->createDescriptor(old,[&](m3::Error error,const auto &value){
            assert(error==m3::Error::NONE && value.size());descriptor=value;
        });assert(created.isOk() && descriptor.size());
        void *buffers[3]={};unsigned stride=0;
        auto allocated=allocator->allocate(descriptor,3,[&](m3::Error error,uint32_t s,const auto &handles){
            assert(error==m3::Error::NONE && handles.size()==3 && s>=w);stride=s;
            for(unsigned i=0;i<3;i++) {
                auto imported=mapper->importBuffer(handles[i],[&](am::Error e,void *buffer){
                    assert(e==am::Error::NONE && buffer);buffers[i]=buffer;
                });assert(imported.isOk() && buffers[i]);
            }
        });assert(allocated.isOk() && stride);
        // Callback-owned transport handles are gone; imports must own their fd lifetime.
        for(unsigned i=0;i<3;i++) {
            auto valid=mapper->validateBufferSize(buffers[i],info,stride);
            assert(valid.isOk() && valid==am::Error::NONE);
            uint64_t width=0,height=0,layers=0,size=0,usage=0,modifier=1,protected_content=1,id=0;
            uint32_t fourcc=0;
            assert(g4::decodeWidth(metadata(buffers[i],g4::MetadataType_Width),&width)==0 && width==w);
            assert(g4::decodeHeight(metadata(buffers[i],g4::MetadataType_Height),&height)==0 && height==h);
            assert(g4::decodeLayerCount(metadata(buffers[i],g4::MetadataType_LayerCount),&layers)==0 && layers==1);
            assert(g4::decodeAllocationSize(metadata(buffers[i],g4::MetadataType_AllocationSize),&size)==0);
            assert(g4::decodeUsage(metadata(buffers[i],g4::MetadataType_Usage),&usage)==0 && usage==info.usage);
            assert(g4::decodePixelFormatModifier(metadata(buffers[i],g4::MetadataType_PixelFormatModifier),&modifier)==0 && modifier==0);
            assert(g4::decodeProtectedContent(metadata(buffers[i],g4::MetadataType_ProtectedContent),&protected_content)==0 && protected_content==0);
            assert(g4::decodeBufferId(metadata(buffers[i],g4::MetadataType_BufferId),&id)==0);
            assert(g4::decodePixelFormatFourCC(metadata(buffers[i],g4::MetadataType_PixelFormatFourCC),&fourcc)==0);
            uint32_t expected=format==22?0x48344241u:format==43?0x30334241u:0x34324241u;
            assert(fourcc==expected);
            android::hardware::graphics::common::V1_2::PixelFormat requested;
            assert(g4::decodePixelFormatRequested(metadata(buffers[i],g4::MetadataType_PixelFormatRequested),&requested)==0 && requested==info.format);
            gc::ExtendableType compression;
            assert(g4::decodeCompression(metadata(buffers[i],g4::MetadataType_Compression),&compression)==0 && compression==g4::Compression_None);
            std::vector<gc::PlaneLayout> planes;
            assert(g4::decodePlaneLayouts(metadata(buffers[i],g4::MetadataType_PlaneLayouts),&planes)==0 && planes.size()==1);
            auto &plane=planes[0];
            assert(plane.offsetInBytes==0 && plane.sampleIncrementInBits==pixel_bytes*8 &&
                plane.strideInBytes==stride*pixel_bytes && plane.widthInSamples==w &&
                plane.heightInSamples==h && plane.totalSizeInBytes<=static_cast<int64_t>(size) &&
                plane.totalSizeInBytes>=plane.strideInBytes*plane.heightInSamples &&
                plane.horizontalSubsampling==1 && plane.verticalSubsampling==1 && plane.components.size()==4);
            int bits=0;
            const gc::PlaneLayoutComponentType types[]={gc::PlaneLayoutComponentType::R,gc::PlaneLayoutComponentType::G,gc::PlaneLayoutComponentType::B,gc::PlaneLayoutComponentType::A};
            for(unsigned c=0;c<4;c++){
                assert(plane.components[c].type.name==GRALLOC4_STANDARD_PLANE_LAYOUT_COMPONENT_TYPE &&
                    plane.components[c].type.value==static_cast<int64_t>(types[c]) && plane.components[c].offsetInBits==bits);
                int count=format==22?16:format==43?(c==3?2:10):8;
                assert(plane.components[c].sizeInBits==count);bits+=count;
            }
            auto unsupported=mapper->get(buffers[i],g4::MetadataType_Smpte2086,[](auto e,const auto &v){assert(e==am::Error::UNSUPPORTED && v.size()==0);});
            assert(unsupported.isOk());
            std::printf("MAPPER4_METADATA id=%llu size=%llu stride=%lld scan=%lld format=%d fourcc=%08x\n",
                (unsigned long long)id,(unsigned long long)size,(long long)plane.strideInBytes,(long long)plane.heightInSamples,format,fourcc);
            am::IMapper::Rect rect{0,0,static_cast<int>(w),static_cast<int>(h)};
            for(unsigned pass=0;pass<2;pass++) {
                bool locked=false;
                auto result=mapper->lock(buffers[i],pass?3:0x30,rect,{},[&](am::Error error,void *data){
                    int32_t bpp=plane.sampleIncrementInBits/8,row=plane.strideInBytes;
                    std::printf("ALLOCATOR_LOCK error=%d data=%p bpp=%d row=%d allocator_stride=%u pass=%u\n",static_cast<int>(error),data,bpp,row,stride,pass);
                    assert(error==am::Error::NONE && data && bpp==static_cast<int>(pixel_bytes) && row==static_cast<int>(stride*pixel_bytes));locked=true;
                    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w*pixel_bytes;x++) {
                        auto *pixel=static_cast<unsigned char *>(data)+static_cast<size_t>(y)*row+x;
                        unsigned char expected=static_cast<unsigned char>((x*17+y*29+i*43)&255);
                        if(pass)assert(*pixel==expected);else *pixel=expected;
                    }
                });assert(result.isOk() && locked);unlock(mapper,buffers[i]);
            }
            auto freed=mapper->freeBuffer(buffers[i]);assert(freed.isOk() && freed==am::Error::NONE);
            auto duplicate=mapper->freeBuffer(buffers[i]);assert(duplicate.isOk() && duplicate==am::Error::BAD_BUFFER);
            verified++;
        }
        std::printf("ALLOCATOR_BATCH format=%d width=%u height=%u stride=%u buffers=3 pixel_exact=1\n",format,w,h,stride);
    }
    std::printf("MAPPER4_IMPORT_METADATA_PASS buffers=%u fields=12 remote_allocator3=1\n",verified);
}
