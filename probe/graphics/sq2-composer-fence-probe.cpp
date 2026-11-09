#include "sq2-composer-client.h"
#include "quest2-mapper5-video-buffer.h"

#include "sq2-composer-gpu-target.h"
#include "sq2-composer-fence-errors.h"

struct Buffer {
    quest2_video_buffer buffer{};
    int release=-1;
    unsigned last=0,uses=0;
    uint64_t id=0;
};
static void fill(Buffer &b,unsigned frame,unsigned slot) {
    if(b.release>=0) {
        wait_fence(b.release,"release",b.last,slot);close(b.release);b.release=-1;
        printf("REUSE_READY frame=%u slot=%u previous=%u time_ns=%lld\n",
            frame,slot,b.last,(long long)now_ns());
    }
    void *data=nullptr;
    assert(!AHardwareBuffer_lock(b.buffer.ahb,AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,-1,nullptr,&data) && data);
    memset(data,0,b.buffer.capacity);
    for(unsigned y=0;y<240;++y)for(unsigned x=0;x<320;++x)
        static_cast<uint8_t*>(data)[size_t(y)*b.buffer.ystride+x]=16+(frame*7+x/20+y/20)%220;
    for(unsigned y=0;y<120;++y)for(unsigned x=0;x<320;x+=2) {
        auto *uv=static_cast<uint8_t*>(data)+b.buffer.uv+size_t(y)*b.buffer.cstride+x;
        uv[0]=80+(frame%4)*20;uv[1]=160-(frame%4)*20;
    }
    int fence=-1;assert(!AHardwareBuffer_unlock(b.buffer.ahb,&fence));
    if(fence>=0) {wait_fence(fence,"cpu",frame,slot);close(fence);}
    printf("BUFFER_WRITE frame=%u slot=%u uses=%u time_ns=%lld\n",frame,slot,b.uses,(long long)now_ns());
}
int main(int argc,char **argv) {
    setvbuf(stdout,nullptr,_IONBF,0);
    bool inspect=argc==2 && !strcmp(argv[1],"--inspect");
    bool gpu=argc==2 && !strcmp(argv[1],"--gpu-targets");
    bool errors=argc==3 && !strcmp(argv[1],"--fence-errors");
    if(argc!=1 && !inspect && !gpu && !errors) {fprintf(stderr,"usage: %s [--inspect|--gpu-targets|--fence-errors SW_SYNC_PATH]\n",argv[0]);return 2;}
    alarm(75);
    android::hardware::configureRpcThreadpool(4,false);
    auto composer=c24::IComposer::getService();assert(composer);
    assert(composer->getCapabilities([](const auto &caps) {
        for(auto c:caps)printf("CAPABILITY value=%d\n",int(c));
    }).isOk());
    if(inspect) {
        assert(composer->dumpDebugInfo([](const auto &dump){printf("%s\n",dump.c_str());}).isOk());
        return 0;
    }
    Client client;client.service=composer;
    auto ret=composer->createClient_2_4([&](c24::Error e,const auto &c){assert(e==c24::Error::NONE && c);client.hal=c;});
    assert(ret.isOk() && client.hal);
    sp<Callback> callback=new Callback;
    assert(client.hal->registerCallback_2_4(callback).isOk());client.display=callback->display();
    uint32_t config=0;
    assert(client.hal->getActiveConfig(client.display,[&](Error e,uint32_t c){assert(e==Error::NONE);config=c;}).isOk());
    int32_t w=0,h=0;
    auto attr=[&](c21::IComposerClient::Attribute a,int32_t *out) {
        assert(client.hal->getDisplayAttribute(client.display,config,a,[&](Error e,int32_t v){assert(e==Error::NONE);*out=v;}).isOk());
    };
    attr(c21::IComposerClient::Attribute::WIDTH,&w);attr(c21::IComposerClient::Attribute::HEIGHT,&h);
    assert(w>=320 && h>=240);
    printf("DISPLAY id=%llu config=%u width=%d height=%d\n",(unsigned long long)client.display,config,w,h);
    auto power=client.hal->setPowerMode(client.display,c21::IComposerClient::PowerMode::ON);
    assert(power.isOk() && power==Error::NONE);
    if(gpu) {
        gpu_targets(client,w,h);client.hal.clear();composer.clear();return 0;
    }
    uint64_t layer=0;
    assert(client.hal->createLayer(client.display,3,[&](Error e,uint64_t l){assert(e==Error::NONE);layer=l;}).isOk());
    Buffer buffers[3];
    for(unsigned slot=0;slot<3;++slot) {
        auto &b=buffers[slot];
        assert(!quest2_video_buffer_allocate(320,240,0x7fa30c04,
            0xb00|AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN|AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,&b.buffer));
        assert(!AHardwareBuffer_getId(b.buffer.ahb,&b.id));
        printf("BUFFER_ID slot=%u id=%llu capacity=%llu ystride=%u cstride=%u uv=%u\n",
            slot,(unsigned long long)b.id,(unsigned long long)b.buffer.capacity,
            b.buffer.ystride,b.buffer.cstride,b.buffer.uv);
    }
    assert(buffers[0].id!=buffers[1].id && buffers[0].id!=buffers[2].id && buffers[1].id!=buffers[2].id);
    client.select(layer);
    client.writer.setLayerCompositionType(c21::IComposerClient::Composition::DEVICE);
    client.writer.setLayerBlendMode(c21::IComposerClient::BlendMode::NONE);
    client.writer.setLayerPlaneAlpha(1.f);client.writer.setLayerZOrder(0);
    client.writer.setLayerDataspace(gc::Dataspace::BT601_625);
    client.writer.setLayerTransform(android::hardware::graphics::common::V1_0::Transform(0));
    client.writer.setLayerSourceCrop({0,0,320,240});
    client.writer.setLayerDisplayFrame({(w-320)/2,(h-240)/2,(w+320)/2,(h+240)/2});
    client.writer.setLayerVisibleRegion({{0,0,w,h}});
    client.check_execute();
    // Match the framework's initial client-target setup before testing DEVICE transitions.
    AHardwareBuffer_Desc desc{};desc.width=w;desc.height=h;desc.layers=1;desc.format=1;
    desc.usage=0xb00|AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN|AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN;
    AHardwareBuffer *target=nullptr;assert(!AHardwareBuffer_allocate(&desc,&target) && target);
    AHardwareBuffer_describe(target,&desc);
    void *pixels=nullptr;
    assert(!AHardwareBuffer_lock(target,AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,-1,nullptr,&pixels) && pixels);
    memset(pixels,0,size_t(desc.stride)*h*4);
    int target_fence=-1;assert(!AHardwareBuffer_unlock(target,&target_fence));
    auto native=reinterpret_cast<PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC>(
        eglGetProcAddress("eglGetNativeClientBufferANDROID"));assert(native);
    auto *window=static_cast<ANativeWindowBuffer_t*>(native(target));assert(window && window->handle);
    auto count=client.hal->setClientTargetSlotCount(client.display,1);
    assert(count.isOk() && count==Error::NONE);
    fill(buffers[0],0,0);
    client.select(layer);
    client.writer.setLayerCompositionType(c21::IComposerClient::Composition::CLIENT);
    client.writer.setLayerBuffer(0,buffers[0].buffer.handle,-1);client.check_execute();
    client.writer.selectDisplay(client.display);
    client.writer.setClientTarget(0,window->handle,target_fence,gc::Dataspace::SRGB,{});client.check_execute();
    client.validate(true);
    client.writer.selectDisplay(client.display);client.writer.presentDisplay();client.check_execute();
    assert(client.reader.result.presents==1 && client.reader.result.present>=0);
    wait_fence(client.reader.result.present,"bootstrap",0,0);
    puts("BOOTSTRAP_COMPLETE");
    for(unsigned frame=0;frame<30;++frame) {
        unsigned slot=frame%3;auto &b=buffers[slot];fill(b,frame,slot);
        if(errors && frame==1)invalid_acquire(client,layer,slot,b.buffer.handle,frame);
        std::unique_ptr<ControlledAcquire> acquire;
        if(errors && frame>=2 && frame<=4)
            acquire.reset(new ControlledAcquire(argv[2],frame,frame!=3,frame!=2));
        client.select(layer);
        client.writer.setLayerCompositionType(c21::IComposerClient::Composition::DEVICE);
        // First use transfers the native handle; later uses exercise the HAL slot cache.
        if(acquire) {acquire->before();fence_error_trace("submit",frame);}
        client.writer.setLayerBuffer(slot,b.uses?nullptr:b.buffer.handle,acquire?acquire->transfer():-1);
        client.writer.setLayerSurfaceDamage({});client.check_execute();
        client.validate();
        client.writer.selectDisplay(client.display);client.writer.presentDisplay();
        if(acquire) {
            auto status=client.execute();
            bool accepted=status==Error::NONE && !client.reader.result.errors &&
                client.reader.result.presents==1 && client.reader.result.present>=0;
            printf("CONTROLLED_SUBMIT_RESULT frame=%u execute=%d command_errors=%u presents=%u accepted=%d\n",
                frame,int(status),client.reader.result.errors,client.reader.result.presents,accepted);
            if(!accepted) {
                acquire->finish();
                assert(!client.reader.result.presents && client.reader.result.releases.empty());
                client.select(layer);client.writer.setLayerBuffer(slot,nullptr,-1);client.check_execute();
                client.validate();client.writer.selectDisplay(client.display);
                client.writer.presentDisplay();client.check_execute();
                printf("CONTROLLED_RETRY frame=%u success=1\n",frame);
            }
        } else client.check_execute();
        auto &r=client.reader.result;
        assert(r.presents==1 && r.present>=0 && r.releases.size()==1 && r.releases[0].first==layer);
        // HIDL release fences belong to the prior frame, per SET_RELEASE_FENCES.
        assert(b.release==-1);
        if(!frame)assert(r.releases[0].second==-1); // Prior layer was CLIENT bootstrap.
        else {
            unsigned previous=(frame-1)%3;auto &retired=buffers[previous];
            assert(retired.last==frame-1 && retired.release==-1 && r.releases[0].second>=0);
            retired.release=r.releases[0].second;r.releases[0].second=-1;
            fence_info(retired.release,"release",frame-1,previous,"returned",false);
            printf("RELEASE_ASSOC presented=%u released=%u slot=%u id=%llu\n",
                frame,frame-1,previous,(unsigned long long)retired.id);
        }
        b.last=frame;++b.uses;
        wait_fence(r.present,"present",frame,slot);
        if(acquire) {acquire->finish();fence_error_trace("present-complete",frame);}
        printf("FRAME_PRESENT frame=%u slot=%u cached=%u layer=%llu time_ns=%lld\n",
            frame,slot,b.uses>1,(unsigned long long)layer,(long long)now_ns());
    }
    // Submit a CLIENT replacement before destruction to retrieve the final DEVICE release.
    client.select(layer);client.writer.setLayerCompositionType(c21::IComposerClient::Composition::CLIENT);
    client.writer.selectDisplay(client.display);
    client.writer.setClientTarget(0,nullptr,-1,gc::Dataspace::SRGB,{});client.check_execute();
    client.validate(true);
    client.writer.selectDisplay(client.display);client.writer.presentDisplay();client.check_execute();
    auto &last=client.reader.result;
    assert(last.presents==1 && last.present>=0 && last.releases.size()==1 && last.releases[0].first==layer);
    assert(buffers[2].last==29 && buffers[2].release==-1 && last.releases[0].second>=0);
    buffers[2].release=last.releases[0].second;last.releases[0].second=-1;
    fence_info(buffers[2].release,"release",29,2,"returned",false);
    printf("RELEASE_ASSOC presented=30 released=29 slot=2 id=%llu\n",(unsigned long long)buffers[2].id);
    wait_fence(last.present,"replacement",30,0);
    auto destroyed=client.hal->destroyLayer(client.display,layer);
    assert(destroyed.isOk() && destroyed==Error::NONE);
    client.validate();
    client.writer.selectDisplay(client.display);client.writer.presentDisplay();client.check_execute();
    assert(client.reader.result.presents==1);
    if(client.reader.result.present>=0)wait_fence(client.reader.result.present,"flush",30,0);
    for(unsigned slot=0;slot<3;++slot) {
        auto &b=buffers[slot];assert(b.uses==10 && b.release>=0);
        wait_fence(b.release,"release",b.last,slot);close(b.release);b.release=-1;
        quest2_video_buffer_release(&b.buffer);
    }
    client.reader.result.close_all();
    AHardwareBuffer_release(target);
    puts("OWNERSHIP_PASS frames=30 slots=3 cached_submissions=27 reuses=27 final_flush=1");
    if(errors)puts("FENCE_ERRORS_GROUP invalid_sync_fd=1 signaled_error=1 pending_signal=1 pending_error=1 real_kernel_sw_sync=1 full_stack_complete=0");
    bool protocol=true;
    for(unsigned version:{21U,22U,23U})for(unsigned scenario=0;scenario<4;++scenario) {
        client.writer.malformed(scenario,client.display);
        Error e=client.execute(version);
        printf("MALFORMED_RESPONSE version=%u scenario=%u actual=%d expected=%d output_errors=%u\n",
            version,scenario,int(e),int(Error::BAD_PARAMETER),client.reader.result.errors);
        protocol&=e==Error::BAD_PARAMETER;
        client.reader.result.close_all();
        client.validate();
        client.writer.selectDisplay(client.display);client.writer.presentDisplay();client.check_execute();
        assert(client.reader.result.presents==1);
        if(client.reader.result.present>=0)
            wait_fence(client.reader.result.present,"recovery",version,scenario);
        client.reader.result.close_all();
        printf("PROTOCOL_RECOVERY version=%u scenario=%u success=1\n",version,scenario);
    }
    if(errors)for(unsigned version:{21U,22U,23U}) {
        client.writer.malformed(4,client.display);auto status=client.execute(version);
        assert(status==Error::BAD_PARAMETER && !client.reader.result.presents && !client.reader.result.errors);
        client.validate();client.writer.selectDisplay(client.display);client.writer.presentDisplay();client.check_execute();
        assert(client.reader.result.presents==1);
        if(client.reader.result.present>=0)wait_fence(client.reader.result.present,"short-recovery",version,0);
        printf("SHORT_CLIENT_TARGET_RESPONSE version=%u actual=%d recovery=1\n",version,int(status));
    }
    client.hal.clear();composer.clear();
    printf("HAL_FENCE_RESULT ownership=1 malformed_error=%d full_stack_complete=0\n",protocol);
    return protocol?0:3;
}
