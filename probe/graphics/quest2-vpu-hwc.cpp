/* Real native VPU buffers are retained until the prior-frame HWC release completes. */
#include "sq2-composer-client.h"
#include "quest2-vpu-hwc.h"
#include <memory>
#include <linux/videodev2.h>
#include <sys/syscall.h>
#include <fcntl.h>

static std::unique_ptr<Client> video_client;
static sp<Callback> video_callback;
static AHardwareBuffer *black_target;
static const native_handle_t *black_handle;
static uint64_t video_layer;
static bool layer_active;
static int display_w,display_h,held_slot=-1;
static unsigned frame_number,stage_number;
static uint64_t held_id;
static bool cached[32];
static gc::Dataspace video_dataspace=gc::Dataspace::BT601_625;

static void video_marker(const char *phase,unsigned slot,uint64_t id,int64_t pts) {
    printf("VPU_HWC phase=%s stage=%u frame=%u slot=%u id=%llu pts=%lld time_ns=%lld\n",
        phase,stage_number,frame_number,slot,(unsigned long long)id,(long long)pts,(long long)now_ns());
    const char *path=getenv("SQ2_COMPOSER_TRACE_MARKER");
    if(!path)return;
    int fd=open(path,O_WRONLY|O_CLOEXEC);assert(fd>=0);
    char line[256];
    int n=snprintf(line,sizeof(line),"SQ2_VPU phase=%s stage=%u frame=%u slot=%u id=%llu pts=%lld pid=%ld time_ns=%lld\n",
        phase,stage_number,frame_number,slot,(unsigned long long)id,(long long)pts,(long)getpid(),(long long)now_ns());
    assert(n>0 && size_t(n)<sizeof(line) && write(fd,line,n)==n);close(fd);
}
extern "C" void quest2_capture_vpu_event(unsigned op,unsigned index,int fd,uint32_t bytes,int64_t pts,uint32_t flags) {
    struct stat st{};assert((op==0 || op==1) && fd>=0 && !fstat(fd,&st));
    printf("VPU_EVENT op=%s index=%u inode=%llu bytes=%u pts=%lld flags=%u time_ns=%lld\n",
        op?"DQBUF":"QBUF",index,(unsigned long long)st.st_ino,bytes,(long long)pts,flags,(long long)now_ns());
}
static void video_connect() {
    if(video_client)return;
    const char *color=getenv("SQ2_COMPOSER_VIDEO_DATASPACE");
    assert(!color || !strcmp(color,"bt601") || !strcmp(color,"bt709"));
    if(color && !strcmp(color,"bt709"))video_dataspace=gc::Dataspace::BT709;
    printf("VPU_INPUT_DATASPACE source=explicit_test_fixture value=0x%08x automatic_OMX_VUI_verified=0\n",unsigned(video_dataspace));
    android::hardware::configureRpcThreadpool(4,false);
    video_client=std::make_unique<Client>();auto &c=*video_client;
    c.service=c24::IComposer::getService();assert(c.service);
    assert(c.service->createClient_2_4([&](c24::Error e,const auto &client) {
        assert(e==c24::Error::NONE && client);c.hal=client;
    }).isOk());
    assert(c.hal);video_callback=new Callback;
    assert(c.hal->registerCallback_2_4(video_callback).isOk());c.display=video_callback->display();
    uint32_t config=0;
    assert(c.hal->getActiveConfig(c.display,[&](Error e,uint32_t value) {
        assert(e==Error::NONE);config=value;
    }).isOk());
    auto attribute=[&](c21::IComposerClient::Attribute a,int *out) {
        assert(c.hal->getDisplayAttribute(c.display,config,a,[&](Error e,int value) {
            assert(e==Error::NONE);*out=value;
        }).isOk());
    };
    attribute(c21::IComposerClient::Attribute::WIDTH,&display_w);
    attribute(c21::IComposerClient::Attribute::HEIGHT,&display_h);
    assert(display_w>0 && display_h>0);
    auto power=c.hal->setPowerMode(c.display,c21::IComposerClient::PowerMode::ON);
    assert(power.isOk() && power==Error::NONE);
    AHardwareBuffer_Desc d{};d.width=display_w;d.height=display_h;d.layers=1;d.format=1;d.usage=0xb33;
    assert(!AHardwareBuffer_allocate(&d,&black_target) && black_target);
    AHardwareBuffer_describe(black_target,&d);void *data=nullptr;
    assert(!AHardwareBuffer_lock(black_target,AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,-1,nullptr,&data) && data);
    memset(data,0,size_t(d.stride)*d.height*4);
    int fence=-1;assert(!AHardwareBuffer_unlock(black_target,&fence));
    if(fence>=0){wait_fence(fence,"vpu_black",0,0);close(fence);}
    auto native=reinterpret_cast<PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC>(
        eglGetProcAddress("eglGetNativeClientBufferANDROID"));assert(native);
    auto *window=static_cast<ANativeWindowBuffer_t*>(native(black_target));
    assert(window && window->handle);black_handle=window->handle;
    auto slots=c.hal->setClientTargetSlotCount(c.display,1);
    assert(slots.isOk() && slots==Error::NONE);
    printf("VPU_DISPLAY display=%llu width=%d height=%d\n",(unsigned long long)c.display,display_w,display_h);
}
static void video_layer_create() {
    auto &c=*video_client;
    assert(!layer_active && held_slot==-1);
    assert(c.hal->createLayer(c.display,32,[&](Error e,uint64_t layer) {
        assert(e==Error::NONE);video_layer=layer;
    }).isOk());
    layer_active=true;
    memset(cached,0,sizeof(cached));
    c.select(video_layer);c.writer.setLayerCompositionType(c21::IComposerClient::Composition::CLIENT);
    c.writer.setLayerBlendMode(c21::IComposerClient::BlendMode::NONE);
    c.writer.setLayerPlaneAlpha(1.f);c.writer.setLayerZOrder(0);
    c.writer.setLayerSourceCrop({0,0,float(display_w),float(display_h)});
    c.writer.setLayerDisplayFrame({0,0,display_w,display_h});c.writer.setLayerVisibleRegion({{0,0,display_w,display_h}});
    c.writer.selectDisplay(c.display);c.writer.setClientTarget(0,black_handle,-1,gc::Dataspace::SRGB,{});
    c.check_execute();c.validate(true);
    c.writer.selectDisplay(c.display);c.writer.presentDisplay();c.check_execute();
    assert(c.reader.result.presents==1 && c.reader.result.present>=0);
    wait_fence(c.reader.result.present,"vpu_bootstrap",stage_number,0);
    printf("VPU_HWC_STAGE_BEGIN stage=%u layer=%llu\n",stage_number,(unsigned long long)video_layer);
}
extern "C" int quest2_hwc_video_present(unsigned slot,const native_handle_t *handle,uint64_t id,
                                      unsigned w,unsigned h,int64_t pts,int *released) {
    assert(slot<32 && handle && id && w && h && !(w&1) && !(h&1) && pts>=0 && released);
    video_connect();if(!layer_active)video_layer_create();
    assert(w<=unsigned(display_w) && h<=unsigned(display_h) && int(slot)!=held_slot);
    *released=-1;auto &c=*video_client;
    video_marker("ready",slot,id,pts);
    c.select(video_layer);c.writer.setLayerCompositionType(c21::IComposerClient::Composition::DEVICE);
    c.writer.setLayerDataspace(video_dataspace);
    c.writer.setLayerSourceCrop({0,0,float(w),float(h)});
    c.writer.setLayerDisplayFrame({(display_w-int(w))/2,(display_h-int(h))/2,
                                   (display_w+int(w))/2,(display_h+int(h))/2});
    c.writer.setLayerBuffer(slot,cached[slot]?nullptr:handle,-1);
    c.writer.setLayerSurfaceDamage({});c.check_execute();c.validate();
    video_marker("present_begin",slot,id,pts);
    c.writer.selectDisplay(c.display);c.writer.presentDisplay();c.check_execute();
    auto &r=c.reader.result;
    assert(r.presents==1 && r.present>=0 && r.releases.size()==1 && r.releases[0].first==video_layer);
    if(held_slot<0)assert(r.releases[0].second<0);
    else {
        assert(r.releases[0].second>=0);
        wait_fence(r.releases[0].second,"vpu_release",frame_number-1,unsigned(held_slot));
        printf("VPU_RELEASE released_frame=%u replaced_by=%u slot=%d id=%llu time_ns=%lld\n",
            frame_number-1,frame_number,held_slot,(unsigned long long)held_id,(long long)now_ns());
        *released=held_slot;
    }
    wait_fence(r.present,"vpu_present",frame_number,slot);
    printf("VPU_PRESENT frame=%u stage=%u slot=%u id=%llu pts=%lld width=%u height=%u cached=%u acquire=completed_DQBUF dataspace=0x%08x time_ns=%lld\n",
        frame_number,stage_number,slot,(unsigned long long)id,(long long)pts,w,h,cached[slot],unsigned(video_dataspace),(long long)now_ns());
    video_marker("present_complete",slot,id,pts);
    held_slot=int(slot);held_id=id;cached[slot]=true;++frame_number;
    c.reader.result.close_all();
    // A bounded observation window for physical KMS/CRC; this is not a latency benchmark.
    timespec hold{0,100000000};while(nanosleep(&hold,&hold) && errno==EINTR){}
    return 0;
}
extern "C" int quest2_hwc_video_flush(int *released) {
    assert(released);*released=-1;
    if(!video_client || !layer_active)return 0;
    auto &c=*video_client;
    if(held_slot>=0) {
        c.select(video_layer);c.writer.setLayerCompositionType(c21::IComposerClient::Composition::CLIENT);
        c.writer.selectDisplay(c.display);c.writer.setClientTarget(0,nullptr,-1,gc::Dataspace::SRGB,{});
        c.check_execute();c.validate(true);
        c.writer.selectDisplay(c.display);c.writer.presentDisplay();c.check_execute();
        auto &r=c.reader.result;
        assert(r.presents==1 && r.present>=0 && r.releases.size()==1 && r.releases[0].first==video_layer && r.releases[0].second>=0);
        wait_fence(r.releases[0].second,"vpu_release",frame_number-1,unsigned(held_slot));
        wait_fence(r.present,"vpu_replacement",frame_number,unsigned(held_slot));
        printf("VPU_RELEASE released_frame=%u replaced_by=%u slot=%d id=%llu time_ns=%lld\n",
            frame_number-1,frame_number,held_slot,(unsigned long long)held_id,(long long)now_ns());
        *released=held_slot;held_slot=-1;
    }
    auto destroy=c.hal->destroyLayer(c.display,video_layer);assert(destroy.isOk() && destroy==Error::NONE);
    c.validate();c.writer.selectDisplay(c.display);c.writer.presentDisplay();c.check_execute();
    assert(c.reader.result.presents==1);
    if(c.reader.result.present>=0)wait_fence(c.reader.result.present,"vpu_flush",stage_number,0);
    c.reader.result.close_all();layer_active=false;
    printf("VPU_HWC_STAGE_END stage=%u frames_total=%u\n",stage_number,frame_number);++stage_number;return 0;
}
extern "C" void quest2_hwc_video_finish() {
    int released=-1;assert(!quest2_hwc_video_flush(&released));
    if(video_client) {
        video_client->reader.result.close_all();video_client->hal.clear();video_client.reset();video_callback.clear();
    }
    if(black_target){AHardwareBuffer_release(black_target);black_target=nullptr;black_handle=nullptr;}
    printf("VPU_HWC_FINISHED frames=%u stages=%u\n",frame_number,stage_number);
}
