#pragma once
/* Bounded real HIDL composer/FMQ ownership diagnostic. No DRM master or fake fences. */
#ifndef LOG_TAG
#define LOG_TAG "Sq2ComposerFence"
#endif
#include <android/hardware/graphics/composer/2.4/IComposer.h>
#include <android/hardware/graphics/composer/2.4/IComposerCallback.h>
#include <composer-command-buffer/2.4/ComposerCommandBuffer.h>
#include <hidl/HidlTransportSupport.h>
#include <android/hardware_buffer.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <system/window.h>
#include <linux/sync_file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <poll.h>
#include <unistd.h>
#include <signal.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <vector>

namespace c21=android::hardware::graphics::composer::V2_1;
namespace c24=android::hardware::graphics::composer::V2_4;
namespace gc=android::hardware::graphics::common::V1_2;
using android::hardware::Return;
using android::hardware::Void;
using android::hardware::hidl_handle;
using android::hardware::hidl_vec;
using android::sp;
using Error=c21::Error;
using Command=c24::IComposerClient::Command;

static int64_t now_ns() {
    timespec t{}; assert(!clock_gettime(CLOCK_MONOTONIC,&t));
    return int64_t(t.tv_sec)*1000000000+t.tv_nsec;
}
class Callback final:public c24::IComposerCallback {
    std::mutex mutex;
    std::condition_variable ready;
    std::vector<uint64_t> displays;
public:
    Return<void> onHotplug(uint64_t d,Connection connected) override {
        std::lock_guard<std::mutex> lock(mutex);
        if(connected==Connection::CONNECTED)displays.push_back(d);
        printf("HOTPLUG display=%llu connection=%d\n",(unsigned long long)d,int(connected));
        ready.notify_all();return Void();
    }
    uint64_t display() {
        std::unique_lock<std::mutex> lock(mutex);
        assert(ready.wait_for(lock,std::chrono::seconds(8),[&]{return !displays.empty();}));
        assert(displays.size()==1);return displays[0];
    }
    Return<void> onRefresh(uint64_t) override {return Void();}
    Return<void> onVsync(uint64_t,int64_t) override {return Void();}
    Return<void> onVsync_2_4(uint64_t,int64_t,uint32_t) override {return Void();}
    Return<void> onVsyncPeriodTimingChanged(uint64_t,const c24::VsyncPeriodChangeTimeline&) override {return Void();}
    Return<void> onSeamlessPossible(uint64_t) override {return Void();}
};
struct Result {
    int present=-1;
    unsigned presents=0,changes=0,client_changes=0,errors=0,requests=0,display_requests=0;
    std::vector<std::pair<uint64_t,int>> releases;
    void close_all() {
        if(present>=0)close(present);
        for(auto &r:releases)if(r.second>=0)close(r.second);
        *this={};
    }
};
class Reader final:public c24::CommandReaderBase {
public:
    Result result;
    void parse(uint64_t display) {
        while(!isEmpty()) {
            Command cmd;uint16_t len=0;assert(beginCommand(&cmd,&len));
            switch(cmd) {
            case Command::SELECT_DISPLAY:
                assert(len==2 && read64()==display);break;
            case Command::SET_ERROR: {
                assert(len==2);auto loc=read();auto err=readSigned();
                printf("COMMAND_ERROR location=%u error=%d\n",loc,err);
                ++result.errors;break;
            }
            case Command::SET_CHANGED_COMPOSITION_TYPES:
                assert(len%3==0);
                for(unsigned i=0;i<len/3;++i) {
                    auto layer=read64();auto type=readSigned();++result.changes;
                    printf("COMPOSITION_CHANGE layer=%llu type=%d\n",(unsigned long long)layer,type);
                    if(type!=int(c21::IComposerClient::Composition::DEVICE))++result.client_changes;
                }
                break;
            case Command::SET_DISPLAY_REQUESTS:
                assert(len>=1 && (len-1)%3==0);
                result.display_requests|=read();
                for(unsigned i=0;i<(len-1)/3;++i) {
                    auto layer=read64();auto request=read();
                    printf("LAYER_REQUEST layer=%llu request=%u\n",(unsigned long long)layer,request);
                    result.requests|=request;
                }
                break;
            case Command::SET_PRESENT_FENCE:
                assert(len==1 && !result.presents++);result.present=readFence();break;
            case Command::SET_RELEASE_FENCES:
                assert(len%3==0);
                for(unsigned i=0;i<len/3;++i) {
                    auto layer=read64();int fence=readFence();result.releases.emplace_back(layer,fence);
                }
                break;
            case Command::SET_CLIENT_TARGET_PROPERTY:
                assert(len==2);read();read();break;
            default:
                fprintf(stderr,"UNEXPECTED_COMMAND opcode=%x length=%u\n",unsigned(cmd),len);abort();
            }
            endCommand();
        }
    }
};
class Writer final:public c24::CommandWriterBase {
public:
    Writer():CommandWriterBase(1024){}
    void malformed(unsigned scenario,uint64_t display) {
        if(scenario<2) {
            beginCommand(Command(0x7fff0000),0);endCommand();
            if(scenario==1)selectDisplay(display);
        } else if(scenario==2) {
            beginCommand(Command::SELECT_DISPLAY,0);endCommand();
            mData[mDataWritten-1]|=2; // A header claiming two missing payload words.
        } else if(scenario==3) {
            beginCommand(Command::SELECT_DISPLAY,1);write(0);endCommand();
        } else {
            assert(scenario==4);beginCommand(Command::SET_CLIENT_TARGET,0);endCommand();
        }
    }
};
class Client {
public:
    sp<c24::IComposerClient> hal;
    sp<c24::IComposer> service;
    unsigned validations=0;
    Writer writer;
    Reader reader;
    uint64_t display=0;
    Error execute(unsigned version=23) {
        reader.result.close_all();reader.reset();
        bool changed=false;uint32_t length=0;hidl_vec<hidl_handle> handles;
        assert(writer.writeQueue(&changed,&length,&handles));
        if(changed) {
            auto desc=writer.getMQDescriptor();assert(desc);
            auto ret=hal->setInputCommandQueue(*desc);assert(ret.isOk() && ret==Error::NONE);
        }
        Error error=Error::NO_RESOURCES;
        auto callback=[&](Error e,bool changed,uint32_t length,const auto &handles) {
            error=e;
            if(changed) {
                auto q=hal->getOutputCommandQueue([&](Error e,const auto &desc) {
                    assert(e==Error::NONE && reader.setMQDescriptor(desc));
                });assert(q.isOk());
            }
            if(length) {assert(reader.readQueue(length,handles));reader.parse(display);}
        };
        auto ret=version==21 ? hal->executeCommands(length,handles,callback) :
                 version==22 ? hal->executeCommands_2_2(length,handles,callback) :
                               hal->executeCommands_2_3(length,handles,callback);
        assert(ret.isOk());writer.reset();
        return error;
    }
    void check_execute() {assert(execute()==Error::NONE && !reader.result.errors);}
    void select(uint64_t layer) {writer.selectDisplay(display);writer.selectLayer(layer);}
    void validate(bool bootstrap=false) {
        writer.selectDisplay(display);writer.validateDisplay();check_execute();
        if(validations++<2 || reader.result.client_changes) {
            assert(service->dumpDebugInfo([](const auto &dump){printf("HAL_DUMP_BEGIN\n%s\nHAL_DUMP_END\n",dump.c_str());}).isOk());
        }
        assert((bootstrap || !reader.result.client_changes) && !(reader.result.requests & ~1U) && !reader.result.display_requests && !reader.result.presents && reader.result.releases.empty());
        if(reader.result.requests)puts("CLEAR_CLIENT_TARGET_HANDLED target_already_transparent=1");
        writer.selectDisplay(display);writer.acceptDisplayChanges();check_execute();
    }
};
static void fence_info(int fd,const char *kind,unsigned frame,unsigned slot,const char *phase,bool require_signaled) {
    assert(fd>=0);
    struct sync_file_info info{};assert(!ioctl(fd,SYNC_IOC_FILE_INFO,&info));
    assert(info.num_fences && info.num_fences<128);
    std::vector<sync_fence_info> points(info.num_fences);
    info.sync_fence_info=reinterpret_cast<uint64_t>(points.data());
    assert(!ioctl(fd,SYNC_IOC_FILE_INFO,&info));
    assert(info.status>=0);
    if(require_signaled)assert(info.status==1);
    struct stat st{};assert(!fstat(fd,&st));
    printf("FENCE kind=%s frame=%u slot=%u phase=%s fd=%d inode=%llu status=%d points=%u time_ns=%lld\n",
        kind,frame,slot,phase,fd,(unsigned long long)st.st_ino,info.status,info.num_fences,(long long)now_ns());
    for(const auto &point:points) {
        assert(point.status>=0);
        if(require_signaled)assert(point.status==1 && point.timestamp_ns);
        printf("FENCE_POINT kind=%s frame=%u slot=%u phase=%s driver=%s object=%s status=%d timestamp_ns=%llu\n",
            kind,frame,slot,phase,point.driver_name,point.obj_name,point.status,
            (unsigned long long)point.timestamp_ns);
    }
}
static void wait_fence(int fd,const char *kind,unsigned frame,unsigned slot) {
    fence_info(fd,kind,frame,slot,"before",false);
    pollfd p{fd,POLLIN,0};int rc;do{rc=poll(&p,1,3000);}while(rc<0 && errno==EINTR);
    assert(rc==1 && p.revents==POLLIN);
    fence_info(fd,kind,frame,slot,"after",true);
}
