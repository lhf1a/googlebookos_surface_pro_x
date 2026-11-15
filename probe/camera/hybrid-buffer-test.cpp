// SPDX-License-Identifier: GPL-2.0-only
// Isolated allocator + mapper test; never replaces the running graphics services.
#define main allocator_service_main
#include "quest2-allocator5-service.cpp"
#undef main
#include <android/hardware/graphics/mapper/IMapper.h>
#include <android/hardware/graphics/mapper/utils/IMapperMetadataTypes.h>
using Type=aidl::android::hardware::graphics::common::StandardMetadataType;
namespace mt=android::hardware::graphics::mapper;
template<Type T> auto metadata(AIMapper* m,buffer_handle_t h) {
    int32_t size=m->v5.getStandardMetadata(h,int64_t(T),nullptr,0);
    assert(size>0);std::vector<uint8_t> bytes(size);
    assert(m->v5.getStandardMetadata(h,int64_t(T),bytes.data(),size)==size);
    auto value=mt::StandardMetadata<T>::value::decode(bytes.data(),bytes.size());
    assert(value);return *value;
}
int main(int argc,char** argv) {
    setvbuf(stdout,nullptr,_IONBF,0);
    if(argc!=2)return 64;
    void* lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!lib){puts(dlerror());return 1;}
    auto load=reinterpret_cast<AIMapper_Error(*)(AIMapper**)>(dlsym(lib,"AIMapper_loadIMapper"));
    AIMapper* m=nullptr;assert(load && load(&m)==0);
    auto allocator=ndk::SharedRefBase::make<QuestAllocator>();
    for(int test:{1,0x32315659,33,35,0x10001}) {
        int format=test==0x10001?1:test;
        aa::BufferDescriptorInfo d{};d.width=format==33?1048576:640;d.height=format==33?1:480;d.layerCount=1;
        d.format=static_cast<aidl::android::hardware::graphics::common::PixelFormat>(format);
        d.usage=static_cast<aidl::android::hardware::graphics::common::BufferUsage>(test==0x10001?0x10033:0x33);
        aa::AllocationResult a;auto r=allocator->allocate2(d,1,&a);
        if(!r.isOk()){printf("allocate format=%x: %s\n",format,r.getDescription().c_str());return 2;}
        assert(a.buffers.size()==1 && a.stride>=d.width);
        auto& n=a.buffers[0];native_handle_t* raw=native_handle_create(n.fds.size(),n.ints.size());
        for(size_t i=0;i<n.fds.size();++i)raw->data[i]=n.fds[i].get();
        std::copy(n.ints.begin(),n.ints.end(),raw->data+n.fds.size());
        // Re-import the transported handle twice; the requested format must survive.
        for(int pass=0;pass<2;pass++) {
            buffer_handle_t imported=nullptr;assert(m->v5.importBuffer(raw,&imported)==0);
            assert(int(metadata<Type::PIXEL_FORMAT_REQUESTED>(m,imported))==format);
            assert(uint64_t(metadata<Type::USAGE>(m,imported))==uint64_t(d.usage));
            assert(metadata<Type::WIDTH>(m,imported)==uint32_t(d.width));
            assert(metadata<Type::HEIGHT>(m,imported)==uint32_t(d.height));
            auto planes=metadata<Type::PLANE_LAYOUTS>(m,imported);
            assert(planes.size()==size_t(format==35 || format==0x32315659?3:1));
            if(format==35)assert(metadata<Type::PIXEL_FORMAT_FOURCC>(m,imported)==0x32315659);
            void* bytes=nullptr;ARect region{0,0,d.width,d.height};
            assert(m->v5.lock(imported,pass?3:0x33,region,-1,&bytes)==0);
            size_t allocation=metadata<Type::ALLOCATION_SIZE>(m,imported);
            for(size_t i=0;i<allocation;++i) {
                auto* p=static_cast<unsigned char*>(bytes);
                if(!pass)p[i]=static_cast<unsigned char>(i*13+7);
                else assert(p[i]==static_cast<unsigned char>(i*13+7));
            }
            int fence=-1;assert(m->v5.unlock(imported,&fence)==0);if(fence>=0)close(fence);
            assert(m->v5.freeBuffer(imported)==0);
        }
        native_handle_delete(raw);
        printf("FORMAT_%x_USAGE_%llx_ALLOC_METADATA_REIMPORT_READ_WRITE_PASS\n",format,(unsigned long long)d.usage);
    }
}
