/* Real HIDL allocator IPC and original mapper import; no service substitution. */
#include <android/hardware/graphics/allocator/3.0/IAllocator.h>
#include <android/hardware/graphics/mapper/3.0/IMapper.h>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <poll.h>
#include <unistd.h>
#include <cstring>
namespace am=android::hardware::graphics::mapper::V3_0;
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
    #ifdef QUEST2_SYSTEM_MAPPER
    auto mapper=am::IMapper::getService("default",true);
    assert(mapper && !mapper->isRemote());
    FILE *maps=std::fopen("/proc/self/maps","r");assert(maps);
    char line[2048];bool hal=false,original=false;
    while(std::fgets(line,sizeof(line),maps)) {
        assert(!std::strstr(line,"libquest2-native-mapper.so"));
        if(std::strstr(line,"android.hardware.graphics.mapper@3.0-impl-quest2.so"))hal=true;
        if(std::strstr(line,"libquest2-original-mapper3.so"))original=true;
    }
    std::fclose(maps);assert(hal && original);
    std::puts("STANDARD_MAPPER_HAL loaded=1 private_getService=0");
#else
    auto mapper=am::IMapper::getService("default");assert(mapper && !mapper->isRemote());
#endif
    unsigned verified=0;
    for(int format:{1,43,22}) for(unsigned w:{64U,129U,1280U}) {
        unsigned pixel_bytes=format==22?8:4;
        unsigned h=w==1280?720:65;
        am::IMapper::BufferDescriptorInfo info{};
        info.width=w;info.height=h;info.layerCount=1;
        info.format=static_cast<android::hardware::graphics::common::V1_2::PixelFormat>(format);
        info.usage=0x133; // CPU_READ/WRITE_OFTEN and GPU_TEXTURE, legacy HIDL usage.
        android::hardware::hidl_vec<uint32_t> descriptor;
        auto created=mapper->createDescriptor(info,[&](am::Error error,const auto &value){
            assert(error==am::Error::NONE && value.size());descriptor=value;
        });assert(created.isOk() && descriptor.size());
        void *buffers[3]={};unsigned stride=0;
        auto allocated=allocator->allocate(descriptor,3,[&](am::Error error,uint32_t s,const auto &handles){
            assert(error==am::Error::NONE && handles.size()==3 && s>=w);stride=s;
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
            am::IMapper::Rect rect{0,0,static_cast<int>(w),static_cast<int>(h)};
            for(unsigned pass=0;pass<2;pass++) {
                bool locked=false;
                auto result=mapper->lock(buffers[i],pass?3:0x30,rect,{},[&](am::Error error,void *data,int32_t bpp,int32_t row){
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
            verified++;
        }
        std::printf("ALLOCATOR_BATCH format=%d width=%u height=%u stride=%u buffers=3 pixel_exact=1\n",format,w,h,stride);
    }
    std::printf("ALLOCATOR_IPC_PASS buffers=%u remote=1 original_mapper=1\n",verified);
}
