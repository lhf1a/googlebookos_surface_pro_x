// SPDX-License-Identifier: GPL-2.0-only
#include "spx-camera-metadata.h"
#include "spx-camera-formats.h"
#include "spx-isp.h"
#include "spx-buffer.h"
#include <aidl/android/hardware/camera/provider/BnCameraProvider.h>
#include <aidl/android/hardware/camera/common/Status.h>
#include <aidl/android/hardware/camera/device/BnCameraDevice.h>
#include <aidl/android/hardware/camera/device/BnCameraDeviceSession.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android/log.h>
#include <vndk/hardware_buffer.h>
#include <algorithm>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>
#include <setjmp.h>
#include <jpeglib.h>
#include <poll.h>
#include <unistd.h>

namespace cd=aidl::android::hardware::camera::device;
namespace cp=aidl::android::hardware::camera::provider;
namespace cc=aidl::android::hardware::camera::common;
namespace gc=aidl::android::hardware::graphics::common;
namespace fmq=aidl::android::hardware::common::fmq;
using Ret=ndk::ScopedAStatus;
using Queue=fmq::MQDescriptor<int8_t,fmq::SynchronizedReadWrite>;
static Ret ok(){return Ret::ok();}
static Ret error(cc::Status s){return Ret::fromServiceSpecificError(int(s));}
static Ret unsupported(){return error(cc::Status::OPERATION_NOT_SUPPORTED);}
static void logError(const char* s){__android_log_print(ANDROID_LOG_ERROR,"SpxCamera","%s",s);}
static std::mutex pipelineMutex;
static bool pipelineOpen=false;
static const char* names[2]={"device@1.1/usb/0","device@1.1/usb/1"};

static bool supported(bool front,const cd::StreamConfiguration& c) {
    if(c.streams.empty() || c.streams.size()>3 || int(c.operationMode)!=0) return false;
    int jpeg=0, processed=0; std::set<int> ids;
    for(const auto& s:c.streams) {
        if(!ids.insert(s.id).second || s.streamType!=cd::StreamType::OUTPUT ||
           !s.physicalCameraId.empty() || int(s.rotation)!=0 ||
           !spx::outputSize(front,s.width,s.height)) return false;
        if(s.format==gc::PixelFormat::BLOB) ++jpeg;
        else if(s.format==gc::PixelFormat::YCBCR_420_888 ||
                s.format==gc::PixelFormat::IMPLEMENTATION_DEFINED) ++processed;
        else return false;
    }
    return jpeg<=1 && processed<=2;
}
struct Buffer {
    AHardwareBuffer* value=nullptr;
    ~Buffer(){if(value) AHardwareBuffer_release(value);}
};
struct JpegError {jpeg_error_mgr base; jmp_buf jump;};
static void jpegFailure(j_common_ptr c){longjmp(reinterpret_cast<JpegError*>(c->err)->jump,1);}
static std::vector<uint8_t> jpeg(const spx::Image& im,int quality,int angle) {
    unsigned w=im.width,h=im.height;
    if(angle!=0 && angle!=90 && angle!=180 && angle!=270) throw std::runtime_error("JPEG orientation");
    std::vector<uint8_t> rotated(im.rgb.size());
    unsigned dw=(angle==90 || angle==270)?h:w, dh=(angle==90 || angle==270)?w:h;
    for(unsigned y=0;y<h;++y) for(unsigned x=0;x<w;++x) {
        unsigned dx=x,dy=y;
        if(angle==90){dx=h-1-y;dy=x;} else if(angle==180){dx=w-1-x;dy=h-1-y;}
        else if(angle==270){dx=y;dy=w-1-x;}
        memcpy(rotated.data()+(dy*dw+dx)*3,im.rgb.data()+(y*w+x)*3,3);
    }
    jpeg_compress_struct c{}; JpegError e{};
    c.err=jpeg_std_error(&e.base);e.base.error_exit=jpegFailure;
    unsigned char* memory=nullptr; unsigned long length=0;
    if(setjmp(e.jump)){jpeg_destroy_compress(&c);free(memory);throw std::runtime_error("JPEG encode");}
    jpeg_create_compress(&c);jpeg_mem_dest(&c,&memory,&length);
    c.image_width=dw;c.image_height=dh;c.input_components=3;c.in_color_space=JCS_RGB;
    jpeg_set_defaults(&c);jpeg_set_quality(&c,std::clamp(quality,1,100),TRUE);
    jpeg_start_compress(&c,TRUE);
    while(c.next_scanline<c.image_height){
        JSAMPROW row=rotated.data()+c.next_scanline*dw*3;
        jpeg_write_scanlines(&c,&row,1);
    }
    jpeg_finish_compress(&c);
    std::vector<uint8_t> result(memory,memory+length);
    jpeg_destroy_compress(&c);free(memory);return result;
}
static void fillBuffer(Buffer& buffer,const cd::Stream& stream,const spx::Image& image,
                       const spx::Meta& settings,int fence) {
    if(fence>=0) {
        pollfd p{fence,POLLIN,0};
        if(poll(&p,1,1000)!=1 || (p.revents&(POLLERR|POLLNVAL))) throw std::runtime_error("acquire fence");
    }
    if(stream.format==gc::PixelFormat::BLOB) {
        auto bytes=jpeg(image,settings.get(ANDROID_JPEG_QUALITY,90),
                        settings.get(ANDROID_JPEG_ORIENTATION,0));
        AHardwareBuffer_Desc desc{};AHardwareBuffer_describe(buffer.value,&desc);
        struct Footer {uint16_t id; uint16_t pad; uint32_t size;};
        if(desc.width<sizeof(Footer) || bytes.size()>desc.width-sizeof(Footer))
            throw std::runtime_error("JPEG buffer too small");
        void* p=nullptr;
        if(AHardwareBuffer_lock(buffer.value,AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,-1,nullptr,&p))
            throw std::runtime_error("lock JPEG buffer");
        memcpy(p,bytes.data(),bytes.size());
        Footer footer{0xff,0,uint32_t(bytes.size())};
        memcpy(static_cast<uint8_t*>(p)+desc.width-sizeof(footer),&footer,sizeof(footer));
        if(AHardwareBuffer_unlock(buffer.value,nullptr)) throw std::runtime_error("unlock JPEG buffer");
        return;
    }
    if(stream.format==gc::PixelFormat::IMPLEMENTATION_DEFINED) {
        void* memory=nullptr; int32_t pixelStride=0,rowStride=0;
        if(AHardwareBuffer_lockAndGetInfo(buffer.value,AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,
                                         -1,nullptr,&memory,&pixelStride,&rowStride))
            throw std::runtime_error("lock RGBA preview");
        if(pixelStride!=4 || rowStride<int(image.width*4)) {
            AHardwareBuffer_unlock(buffer.value,nullptr);
            throw std::runtime_error("invalid RGBA stride");
        }
        for(unsigned y=0;y<image.height;++y) for(unsigned x=0;x<image.width;++x) {
            auto* p=static_cast<uint8_t*>(memory)+y*rowStride+x*4;
            memcpy(p,image.rgb.data()+(y*image.width+x)*3,3);p[3]=255;
        }
        if(AHardwareBuffer_unlock(buffer.value,nullptr)) throw std::runtime_error("unlock RGBA preview");
        return;
    }
    AHardwareBuffer_Planes planes{};
    if(AHardwareBuffer_lockPlanes(buffer.value,AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,-1,nullptr,&planes))
        throw std::runtime_error("lock YUV buffer");
    if(planes.planeCount!=3) {
        AHardwareBuffer_unlock(buffer.value,nullptr);
        throw std::runtime_error("unexpected YUV plane count");
    }
    auto put=[&](unsigned p,unsigned x,unsigned y,int value){
        auto& a=planes.planes[p];
        static_cast<uint8_t*>(a.data)[y*a.rowStride+x*a.pixelStride]=std::clamp(value,0,255);
    };
    for(unsigned y=0;y<image.height;++y) for(unsigned x=0;x<image.width;++x) {
        const auto* p=image.rgb.data()+(y*image.width+x)*3;
        put(0,x,y,((66*p[0]+129*p[1]+25*p[2]+128)>>8)+16);
        if(!(x%2) && !(y%2)) {
            int r=0,g=0,b=0;
            for(unsigned dy=0;dy<2;++dy) for(unsigned dx=0;dx<2;++dx) {
                const auto* q=image.rgb.data()+((y+dy)*image.width+x+dx)*3;
                r+=q[0];g+=q[1];b+=q[2];
            }
            put(1,x/2,y/2,((-38*r-74*g+112*b+512)>>10)+128);
            put(2,x/2,y/2,((112*r-94*g-18*b+512)>>10)+128);
        }
    }
    if(AHardwareBuffer_unlock(buffer.value,nullptr)) throw std::runtime_error("unlock YUV buffer");
}
class Session final:public cd::BnCameraDeviceSession {
    bool front,closed=false,failed=false;
    std::mutex lock;
    std::shared_ptr<cd::ICameraDeviceCallback> callback;
    std::map<int,cd::Stream> streams;
    std::map<std::pair<int,int64_t>,std::shared_ptr<Buffer>> buffers;
    std::unique_ptr<spx::Capture> capture;
    spx::Isp isp;
    spx::Size captureSize;
    spx::Metadata lastSettings;
    std::shared_ptr<Buffer> import(const cd::StreamBuffer& b) {
        auto key=std::make_pair(b.streamId,b.bufferId);
        if(b.buffer.fds.empty() && b.buffer.ints.empty()) {
            auto it=buffers.find(key); if(it==buffers.end()) throw std::runtime_error("unknown buffer ID");
            return it->second;
        }
        const auto& stream=streams.at(b.streamId);
        auto* handle=native_handle_create(b.buffer.fds.size(),b.buffer.ints.size());
        if(!handle) throw std::bad_alloc();
        for(size_t i=0;i<b.buffer.fds.size();++i) handle->data[i]=b.buffer.fds[i].get();
        std::copy(b.buffer.ints.begin(),b.buffer.ints.end(),handle->data+b.buffer.fds.size());
        AHardwareBuffer_Desc d{};d.layers=1;d.width=stream.width;d.height=stream.height;
        d.format=stream.format==gc::PixelFormat::IMPLEMENTATION_DEFINED ?
                 AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM : AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420;
        d.usage=AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN|uint64_t(stream.usage);
        if(stream.format==gc::PixelFormat::BLOB) {
            d.format=AHARDWAREBUFFER_FORMAT_BLOB;d.width=stream.bufferSize>0?stream.bufferSize:spx::jpegMaxSize(front);d.height=1;
        }
        try { d=spx::describeBuffer(handle); }
        catch(...) {native_handle_delete(handle);throw;}
        auto result=std::make_shared<Buffer>();
        int r=AHardwareBuffer_createFromHandle(&d,handle,AHARDWAREBUFFER_CREATE_FROM_HANDLE_METHOD_CLONE,&result->value);
        native_handle_delete(handle);
        if(r) throw std::runtime_error("import gralloc buffer: "+std::to_string(r));
        buffers[key]=result;return result;
    }
public:
    Session(bool f,std::shared_ptr<cd::ICameraDeviceCallback> c):front(f),callback(std::move(c)){}
    ~Session(){close();}
    Ret close() override {
        std::lock_guard<std::mutex> guard(lock);
        if(!closed) {
            capture.reset();buffers.clear();streams.clear();closed=true;
            std::lock_guard<std::mutex> pipe(pipelineMutex);pipelineOpen=false;
        }return ok();
    }
    Ret configureStreams(const cd::StreamConfiguration& c,std::vector<cd::HalStream>* out) override {
        std::lock_guard<std::mutex> guard(lock);
        if(closed || !supported(front,c)) return error(cc::Status::ILLEGAL_ARGUMENT);
        capture.reset();buffers.clear();streams.clear();lastSettings.metadata.clear();isp=spx::Isp();
        captureSize=spx::binnedSize(front);
        for(const auto& s:c.streams) {
            if(s.width>captureSize.first || s.height>captureSize.second)captureSize=spx::nativeSize(front);
            streams[s.id]=s;cd::HalStream h;h.id=s.id;
            h.overrideFormat=s.format==gc::PixelFormat::IMPLEMENTATION_DEFINED?gc::PixelFormat::RGBA_8888:s.format;
            h.producerUsage=gc::BufferUsage::CPU_WRITE_OFTEN;h.maxBuffers=4;
            h.overrideDataSpace=s.dataSpace;out->push_back(h);
        }return ok();
    }
    Ret configureStreamsV2(const cd::StreamConfiguration& c,cd::ConfigureStreamsRet* r) override {
        return configureStreams(c,&r->halStreams);
    }
    Ret constructDefaultRequestSettings(cd::RequestTemplate t,cd::CameraMetadata* out) override {
        if(int(t)<1 || int(t)>5) return error(cc::Status::ILLEGAL_ARGUMENT);
        *out=spx::defaults(front,int(t));return ok();
    }
    Ret flush() override {std::lock_guard<std::mutex> guard(lock);capture.reset();return ok();}
    Ret getCaptureRequestMetadataQueue(Queue*) override {return ok();}
    Ret getCaptureResultMetadataQueue(Queue*) override {return ok();}
    Ret isReconfigurationRequired(const cd::CameraMetadata&,const cd::CameraMetadata&,bool* b) override {*b=false;return ok();}
    Ret signalStreamFlush(const std::vector<int32_t>&,int32_t) override {return flush();}
    Ret switchToOffline(const std::vector<int32_t>&,cd::CameraOfflineSessionInfo*,
                        std::shared_ptr<cd::ICameraOfflineSession>*) override {return unsupported();}
    Ret repeatingRequestEnd(int32_t,const std::vector<int32_t>&) override {return ok();}
    Ret processCaptureRequest(const std::vector<cd::CaptureRequest>& requests,
                              const std::vector<cd::BufferCache>& remove,int32_t* accepted) override {
        std::lock_guard<std::mutex> guard(lock);*accepted=0;
        if(closed || failed || streams.empty()) return error(cc::Status::INTERNAL_ERROR);
        for(const auto& c:remove) buffers.erase({c.streamId,c.bufferId});
        for(const auto& req:requests) {
            if(req.fmqSettingsSize || req.outputBuffers.empty() || req.inputBuffer.streamId!=-1)
                return error(cc::Status::ILLEGAL_ARGUMENT);
            if(!req.settings.metadata.empty()) lastSettings=req.settings;
            if(lastSettings.metadata.empty()) return error(cc::Status::ILLEGAL_ARGUMENT);
            for(const auto& b:req.outputBuffers) if(!streams.count(b.streamId) || !b.bufferId)
                return error(cc::Status::ILLEGAL_ARGUMENT);
            cd::CaptureResult result;result.frameNumber=req.frameNumber;result.inputBuffer.streamId=-1;
            try {
                spx::Meta settings(lastSettings);
                std::vector<std::shared_ptr<Buffer>> output;
                for(const auto& b:req.outputBuffers) output.push_back(import(b));
                if(!capture) {
                    capture=std::make_unique<spx::Capture>(front,captureSize.first,captureSize.second,30);
                    capture->next();capture->next();
                }
                auto frame=capture->next();
                cd::ShutterMsg shutter;shutter.frameNumber=req.frameNumber;shutter.timestamp=frame.timestampNs;
                callback->notify({cd::NotifyMsg::make<cd::NotifyMsg::shutter>(shutter)});
                bool fullResolution=false;
                for(const auto& b:req.outputBuffers) {
                    const auto& stream=streams.at(b.streamId);
                    fullResolution|=unsigned(stream.width)>frame.width/2 || unsigned(stream.height)>frame.height/2;
                }
                isp.prepare(frame,fullResolution);
                std::map<spx::Size,spx::Image> images;
                bool aeDone=false;
                for(size_t i=0;i<req.outputBuffers.size();++i) {
                    const auto& b=req.outputBuffers[i];const auto& s=streams.at(b.streamId);
                    auto key=spx::Size{s.width,s.height};
                    auto it=images.find(key);
                    if(it==images.end())it=images.emplace(key,isp.render(s.width,s.height)).first;
                    const auto& image=it->second;
                    if(!aeDone){capture->autoExposure(image.linearMean);aeDone=true;}
                    fillBuffer(*output[i],s,image,settings,b.acquireFence.fds.empty()?-1:b.acquireFence.fds[0].get());
                    cd::StreamBuffer done;done.streamId=b.streamId;done.bufferId=b.bufferId;
                    done.status=cd::BufferStatus::OK;result.outputBuffers.push_back(std::move(done));
                }
                settings.set<int64_t>(ANDROID_SENSOR_TIMESTAMP,{int64_t(frame.timestampNs)});
                settings.set<uint8_t>(ANDROID_CONTROL_AE_STATE,{ANDROID_CONTROL_AE_STATE_SEARCHING});
                settings.set<uint8_t>(ANDROID_CONTROL_AF_STATE,{ANDROID_CONTROL_AF_STATE_INACTIVE});
                settings.set<uint8_t>(ANDROID_CONTROL_AWB_STATE,{ANDROID_CONTROL_AWB_STATE_SEARCHING});
                settings.set<uint8_t>(ANDROID_FLASH_STATE,{ANDROID_FLASH_STATE_UNAVAILABLE});
                settings.set<uint8_t>(ANDROID_LENS_STATE,{ANDROID_LENS_STATE_STATIONARY});
                settings.set<uint8_t>(ANDROID_REQUEST_PIPELINE_DEPTH,{1});
                settings.set<uint8_t>(ANDROID_STATISTICS_SCENE_FLICKER,{0});
                result.result=settings.pack();result.partialResult=1;
            } catch(const std::exception& e) {
                logError(e.what());capture.reset();failed=true;
                cd::ErrorMsg msg;msg.frameNumber=req.frameNumber;msg.errorStreamId=-1;msg.errorCode=cd::ErrorCode::ERROR_REQUEST;
                callback->notify({cd::NotifyMsg::make<cd::NotifyMsg::error>(msg)});
                result.outputBuffers.clear();
                for(const auto& b:req.outputBuffers) {
                    cd::StreamBuffer done;done.streamId=b.streamId;done.bufferId=b.bufferId;done.status=cd::BufferStatus::ERROR;
                    for(const auto& f:b.acquireFence.fds) done.releaseFence.fds.emplace_back(dup(f.get()));
                    result.outputBuffers.push_back(std::move(done));
                }
            }
            std::vector<cd::CaptureResult> results;results.push_back(std::move(result));
            if(!callback->processCaptureResult(results).isOk()) {capture.reset();return error(cc::Status::INTERNAL_ERROR);}
            ++*accepted;
            if(failed) {
                // Do not restart a failed runtime-PM/pipeline state for every queued request.
                cd::ErrorMsg msg;msg.errorStreamId=-1;msg.errorCode=cd::ErrorCode::ERROR_DEVICE;
                callback->notify({cd::NotifyMsg::make<cd::NotifyMsg::error>(msg)});
                return ok();
            }
        }return ok();
    }
};
class Device final:public cd::BnCameraDevice {
    bool front;
public:
    explicit Device(bool f):front(f){}
    Ret getCameraCharacteristics(cd::CameraMetadata* m) override {*m=spx::characteristics(front);return ok();}
    Ret getPhysicalCameraCharacteristics(const std::string&,cd::CameraMetadata*) override{return error(cc::Status::ILLEGAL_ARGUMENT);}
    Ret getResourceCost(cc::CameraResourceCost* c) override {
        c->resourceCost=100;c->conflictingDevices={names[front?0:1]};return ok();
    }
    Ret isStreamCombinationSupported(const cd::StreamConfiguration& c,bool* b) override {
        *b=supported(front,c);
        for(const auto& s:c.streams)
            __android_log_print(ANDROID_LOG_INFO,"SpxCamera","stream query supported=%d format=%d %dx%d usage=%llx",
                *b,int(s.format),s.width,s.height,(unsigned long long)s.usage);
        return ok();
    }
    Ret isStreamCombinationWithSettingsSupported(const cd::StreamConfiguration& c,bool* b) override{return isStreamCombinationSupported(c,b);}
    Ret getSessionCharacteristics(const cd::StreamConfiguration&,cd::CameraMetadata*) override{return unsupported();}
    Ret constructDefaultRequestSettings(cd::RequestTemplate t,cd::CameraMetadata* m) override {
        if(int(t)<1 || int(t)>5) return error(cc::Status::ILLEGAL_ARGUMENT);
        *m=spx::defaults(front,int(t));return ok();
    }
    Ret open(const std::shared_ptr<cd::ICameraDeviceCallback>& c,std::shared_ptr<cd::ICameraDeviceSession>* s) override {
        if(!c) return error(cc::Status::ILLEGAL_ARGUMENT);
        std::lock_guard<std::mutex> guard(pipelineMutex);
        if(pipelineOpen) return error(cc::Status::MAX_CAMERAS_IN_USE);
        *s=ndk::SharedRefBase::make<Session>(front,c);pipelineOpen=true;return ok();
    }
    Ret openInjectionSession(const std::shared_ptr<cd::ICameraDeviceCallback>&,
                             std::shared_ptr<cd::ICameraInjectionSession>*) override{return unsupported();}
    Ret setTorchMode(bool) override{return error(cc::Status::OPERATION_NOT_SUPPORTED);}
    Ret turnOnTorchWithStrengthLevel(int32_t) override{return error(cc::Status::OPERATION_NOT_SUPPORTED);}
    Ret getTorchStrengthLevel(int32_t*) override{return error(cc::Status::OPERATION_NOT_SUPPORTED);}
};
class Provider final:public cp::BnCameraProvider {
    std::shared_ptr<cp::ICameraProviderCallback> callback;
public:
    Ret setCallback(const std::shared_ptr<cp::ICameraProviderCallback>& c) override {
        if(!c)return error(cc::Status::ILLEGAL_ARGUMENT);
        callback=c;return ok();
    }
    Ret getVendorTags(std::vector<cc::VendorTagSection>*) override{return ok();}
    Ret getCameraIdList(std::vector<std::string>* ids) override {*ids={names[0],names[1]};return ok();}
    Ret getCameraDeviceInterface(const std::string& n,std::shared_ptr<cd::ICameraDevice>* d) override {
        if(n!=names[0] && n!=names[1])return error(cc::Status::ILLEGAL_ARGUMENT);
        *d=ndk::SharedRefBase::make<Device>(n==names[1]);return ok();
    }
    Ret notifyDeviceStateChange(int64_t) override{return ok();}
    Ret getConcurrentCameraIds(std::vector<cp::ConcurrentCameraIdCombination>*) override{return ok();}
    Ret isConcurrentStreamCombinationSupported(const std::vector<cp::CameraIdAndStreamCombination>&,bool* b) override {*b=false;return ok();}
};
int main(int argc,char** argv) {
    try {
        // Validate metadata before registering; malformed characteristics must not reach cameraserver.
        for(bool front:{false,true}) {spx::Meta c(spx::characteristics(front));}
        if(argc==2 && !strcmp(argv[1],"--metadata-test")) {puts("front/rear metadata valid");return 0;}
        ABinderProcess_setThreadPoolMaxThreadCount(4);
        auto provider=ndk::SharedRefBase::make<Provider>();
        binder_status_t r=AServiceManager_addService(provider->asBinder().get(),
            "android.hardware.camera.provider.ICameraProvider/usb/0");
        if(r){fprintf(stderr,"register provider: %d\n",r);return 1;}
        ABinderProcess_joinThreadPool();
    } catch(const std::exception& e){logError(e.what());fprintf(stderr,"%s\n",e.what());return 1;}
}
