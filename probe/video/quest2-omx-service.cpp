// Platform HIDL transport and XML parser, unmodified OEM OMX plugin.
#include <hidl/HidlTransportSupport.h>
#include <media/stagefright/omx/1.0/OmxStore.h>
#include <cstdio>
#include <cerrno>
#include <mutex>
#include <set>
#include <dlfcn.h>
#include <android/log.h>
#ifdef QUEST2_NATIVE_SURFACE_TEST
#include "quest2-vidc-node.h"
#include "quest2-omx-node-usage.h"
#include "quest2-omx-native-layout.h"
#endif
#include "quest2-omx-port-abi.h"
#include "quest2-omx-caps.h"
#include <OMX_IndexExt.h>
#include <AshmemAllocator.h>
using android::hardware::media::omx::V1_0::IOmx;
using android::hardware::media::omx::V1_0::implementation::OmxStore;
extern "C" IOmx* HIDL_FETCH_IOmx(const char*);

using namespace android::hardware::media::omx::V1_0;
using android::hardware::Return;
using android::hardware::hidl_vec;
using android::sp;
static_assert(sizeof(quest2_port32)==96 && sizeof(OMX_PARAM_PORTDEFINITIONTYPE)==112);
static std::map<std::string,Quest2CodecCaps> codecCaps;
#ifdef QUEST2_NATIVE_SURFACE_TEST
// Candidate only; normal builds retain SoftwareRenderer until native playback,
// reconfiguration and lifetime are verified.
static constexpr bool nativeSurfaceCapable=true;
#define Q2_NODE_LOG(...) __android_log_print(ANDROID_LOG_INFO,"Q2OMXNODE",__VA_ARGS__)
static constexpr uint32_t consumerUsageIndex=0x7f715532;
static constexpr const char *consumerUsageName=
 "OMX.google.android.index.AndroidNativeBufferConsumerUsage";
struct Quest2NodeBridge {
 decltype(&quest2_vidc_node_begin) begin;
 decltype(&quest2_vidc_node_end) end;
 decltype(&quest2_vidc_node_configure) configure;
 decltype(&quest2_vidc_node_get_layout) layout;
 Quest2NodeBridge():
  begin(reinterpret_cast<decltype(begin)>(dlsym(RTLD_DEFAULT,"quest2_vidc_node_begin"))),
  end(reinterpret_cast<decltype(end)>(dlsym(RTLD_DEFAULT,"quest2_vidc_node_end"))),
  configure(reinterpret_cast<decltype(configure)>(dlsym(RTLD_DEFAULT,"quest2_vidc_node_configure"))),
  layout(reinterpret_cast<decltype(layout)>(dlsym(RTLD_DEFAULT,"quest2_vidc_node_get_layout"))){}
 bool valid() const{return begin&&end&&configure&&layout;}
};
static const Quest2NodeBridge& nodeBridge(){static Quest2NodeBridge bridge;return bridge;}
static Status bridgeStatus(int error){
 switch(error){
  case ENOMEM:return Status::NO_MEMORY;
  case ENODEV:return Status::DEAD_OBJECT;
  case EINVAL:return Status::BAD_VALUE;
  case EBUSY:return Status::INVALID_OPERATION;
  case EAGAIN:return Status::WOULD_BLOCK;
  case ENOSYS:case EOPNOTSUPP:return Status::ERROR_UNSUPPORTED;
  default:return Status::UNKNOWN_ERROR;
 }
}
static bool consumerParameter(const hidl_vec<uint8_t>& input,OMX_PARAM_U32TYPE *p){
 static_assert(sizeof(*p)==16&&offsetof(OMX_PARAM_U32TYPE,nVersion)==4&&
  offsetof(OMX_PARAM_U32TYPE,nPortIndex)==8&&offsetof(OMX_PARAM_U32TYPE,nU32)==12);
 uint32_t usage=0;
 if(quest2_omx_decode_consumer_usage(input.data(),input.size(),&usage))return false;
 memcpy(p,input.data(),sizeof(*p));return true;
}
#else
static constexpr bool nativeSurfaceCapable=false;
#endif
class Quest2Node final : public IOmxNode {
 sp<IOmxNode> base_;
 const Quest2CodecCaps& caps_;
#ifdef QUEST2_NATIVE_SURFACE_TEST
 mutable std::mutex stateMutex_;
 uint64_t token_;
 PortMode outputMode_=PortMode::PRESET_BYTE_BUFFER;
 uint32_t consumerUsage_=0,producerUsage_=0;
 bool consumerValid_=false,producerValid_=false,failed_=false;
 std::set<uint32_t> outputBuffers_;
 // Pinned ACodec adds kVideoGrallocUsage; libui removes two legacy hints.
 // This retains every actual consumer/producer bit outside that known transform.
 static uint64_t allocationUsage(uint32_t consumer,uint32_t producer){
  return quest2_omx_allocation_usage(consumer,producer);
 }
 Status configureLocked(PortMode mode,uint32_t consumer,bool consumerValid,
                        uint32_t producer,bool producerValid){
  if(!token_||failed_)return Status::DEAD_OBJECT;
  const bool native=mode==PortMode::PRESET_ANW_BUFFER;
  const bool known=native&&consumerValid&&producerValid;
  const uint64_t usage=known?allocationUsage(consumer,producer):0;
  const int result=nodeBridge().configure(token_,native,usage,known),error=result?errno:0;
  Q2_NODE_LOG("configure token=%llu native=%d consumer=%x/%d producer=%x/%d usage=%llx error=%d",
   (unsigned long long)token_,native,consumer,consumerValid,producer,producerValid,
   (unsigned long long)usage,error);
  return result?bridgeStatus(error):Status::OK;
 }
 bool nativeOutput() const{
  std::lock_guard<std::mutex> lock(stateMutex_);
  return outputMode_==PortMode::PRESET_ANW_BUFFER;
 }
 Status validateNativeLocked(const CodecBuffer& buffer){
  if(!token_||failed_)return Status::DEAD_OBJECT;
  if(!consumerValid_||!producerValid_)return Status::INVALID_OPERATION;
  if(buffer.type!=CodecBuffer::Type::ANW_BUFFER)return Status::BAD_VALUE;
  // The actual OMX port describes allocation dimensions. Kernel COMPOSE is
  // visible crop; existing native ANW can be padded (320x512 for 320x240).
  OMX_PARAM_PORTDEFINITIONTYPE port{};
  port.nSize=sizeof(port);port.nVersion.nVersion=1;port.nPortIndex=1;
  hidl_vec<uint8_t> input;input.setToExternal(reinterpret_cast<uint8_t*>(&port),sizeof(port));
  Status status=Status::UNKNOWN_ERROR;bool called=false;
  auto result=base_->getParameter(OMX_IndexParamPortDefinition,input,
   [&](Status s,const hidl_vec<uint8_t>& bytes){
    called=true;status=s;
    if(s==Status::OK){
     if(bytes.size()!=sizeof(port)){status=Status::BAD_VALUE;return;}
     memcpy(&port,bytes.data(),sizeof(port));
    }
   });
  if(!result.isOk()||!called)return Status::UNKNOWN_ERROR;
  if(status!=Status::OK)return status;
  const uint32_t format=port.format.video.eColorFormat;
  const unsigned sample=quest2_omx_native_sample_bytes(format);
  if(port.nPortIndex!=1||port.eDomain!=OMX_PortDomainVideo||!sample||
     (sample==2&&!caps_.p010))return Status::ERROR_UNSUPPORTED;
  const uint32_t allocationWidth=port.format.video.nFrameWidth;
  const uint32_t allocationHeight=port.format.video.nFrameHeight;
  quest2_vidc_node_layout layout{};
  if(nodeBridge().layout(token_,&layout)){
   const int error=errno;
   if(error!=EAGAIN){
    Q2_NODE_LOG("validate stage=session-layout token=%llu error=%d",(unsigned long long)token_,error);
    return bridgeStatus(error);
   }
   // Only before first SOURCE_CHANGE: predict from the real port/allocator;
   // no parsed crop is invented by this initial registration path.
   layout.width=allocationWidth;layout.height=allocationHeight;
   layout.format=quest2_omx_native_v4l2_format(format);layout.usage=allocationUsage(consumerUsage_,producerUsage_);
   const int predicted=quest2_omx_capture_layout_for_usage_v2(layout.width,layout.height,layout.format,
      layout.usage,port.nBufferSize,&layout.stride,&layout.scan,&layout.size);
   if(predicted){
    Q2_NODE_LOG("validate stage=initial-allocation token=%llu port=%ux%u bytes=%u usage=%llx error=%d",
     (unsigned long long)token_,layout.width,layout.height,port.nBufferSize,
     (unsigned long long)layout.usage,predicted);return bridgeStatus(-predicted);
   }
  }
  if(layout.format!=quest2_omx_native_v4l2_format(format)||layout.stride%sample||layout.usage!=allocationUsage(consumerUsage_,producerUsage_)){
   Q2_NODE_LOG("validate stage=session-contract token=%llu format=%x usage=%llx expected_usage=%llx",
    (unsigned long long)token_,layout.format,(unsigned long long)layout.usage,
    (unsigned long long)allocationUsage(consumerUsage_,producerUsage_));return Status::ERROR_UNSUPPORTED;
  }
  if(!quest2_omx_allocation_fits_capture(layout.width,layout.height,allocationWidth,allocationHeight,
     layout.stride/sample,layout.scan,port.nBufferSize,layout.size)){
   Q2_NODE_LOG("validate stage=allocation-bounds token=%llu visible=%ux%u port=%ux%u port_bytes=%u storage=%ux%u bytes=%u",
    (unsigned long long)token_,layout.width,layout.height,allocationWidth,allocationHeight,
    port.nBufferSize,layout.stride,layout.scan,layout.size);return Status::BAD_VALUE;
  }
  const auto& a=buffer.attr.anwBuffer;
  if(outputBuffers_.empty())Q2_NODE_LOG(
   "validate-first token=%llu ANW=%ux%u stride=%u format=%x layers=%llu raw_usage=%llx port=%ux%u visible=%ux%u stride=%u scan=%u bytes=%u usage=%llx",
   (unsigned long long)token_,a.width,a.height,a.stride,uint32_t(a.format),(unsigned long long)a.layerCount,
   (unsigned long long)a.usage,allocationWidth,allocationHeight,layout.width,layout.height,layout.stride,layout.scan,layout.size,
   (unsigned long long)layout.usage);
  if(a.width!=allocationWidth||a.height!=allocationHeight||a.stride!=layout.stride/sample||
     uint32_t(a.format)!=format||a.layerCount!=1||
     a.usage!=quest2_omx_requested_usage(consumerUsage_,producerUsage_)||
     (a.usage&~0x2400ULL)!=layout.usage){
   Q2_NODE_LOG("validate stage=anw-attributes token=%llu ANW=%ux%u stride=%u format=%x layers=%llu raw_usage=%llx port=%ux%u visible=%ux%u stride=%u scan=%u bytes=%u expected_raw_usage=%llx",
    (unsigned long long)token_,a.width,a.height,a.stride,uint32_t(a.format),(unsigned long long)a.layerCount,
    (unsigned long long)a.usage,allocationWidth,allocationHeight,layout.width,layout.height,layout.stride,layout.scan,layout.size,
    (unsigned long long)quest2_omx_requested_usage(consumerUsage_,producerUsage_));
   return Status::BAD_VALUE;
  }
  const int imported=quest2_omx_validate_native_buffer(buffer.nativeHandle.getNativeHandle(),
     allocationWidth,allocationHeight,format,layout.usage,
     layout.stride,layout.scan,layout.size);
  if(imported){
   Q2_NODE_LOG("validate stage=real-import token=%llu allocation=%ux%u stride=%u scan=%u bytes=%u usage=%llx error=%d",
    (unsigned long long)token_,allocationWidth,allocationHeight,layout.stride,layout.scan,layout.size,
    (unsigned long long)layout.usage,imported);return bridgeStatus(-imported);
  }
  return Status::OK;
 }
#else
 bool nativeOutput() const{return false;}
#endif
public:
#ifdef QUEST2_NATIVE_SURFACE_TEST
 explicit Quest2Node(sp<IOmxNode> base,const Quest2CodecCaps& caps,uint64_t token):
  base_(std::move(base)),caps_(caps),token_(token){}
#else
 explicit Quest2Node(sp<IOmxNode> base,const Quest2CodecCaps& caps):base_(std::move(base)),caps_(caps){}
#endif
 Return<void> getParameter(uint32_t index,const hidl_vec<uint8_t>& input,getParameter_cb cb) override {
  const bool nativeSurface=nativeOutput();
#ifdef QUEST2_NATIVE_SURFACE_TEST
  if(index==consumerUsageIndex){
   OMX_PARAM_U32TYPE p{};
   if(!consumerParameter(input,&p)){cb(Status::BAD_VALUE,{});return {};}
   Status status=Status::OK;
   {std::lock_guard<std::mutex> lock(stateMutex_);
    if(!token_||failed_)status=Status::DEAD_OBJECT;
    else if(!consumerValid_)status=Status::INVALID_OPERATION;
    else p.nU32=consumerUsage_;
   }
   hidl_vec<uint8_t> result;
   if(status==Status::OK){result.resize(sizeof(p));memcpy(result.data(),&p,sizeof(p));}
   cb(status,result);return {};
  }
#endif
  if(index==OMX_IndexParamVideoProfileLevelQuerySupported){
   OMX_VIDEO_PARAM_PROFILELEVELTYPE p{};
   if(input.size()!=sizeof(p)){cb(Status::BAD_VALUE,{});return {};}
   memcpy(&p,input.data(),sizeof(p));
   if(p.nSize!=sizeof(p)||p.nPortIndex!=0){cb(Status::BAD_VALUE,{});return {};}
   if(p.nProfileIndex>=caps_.profiles.size()){cb(Status::ERROR_UNSUPPORTED,{});return {};}
   const auto& actual=caps_.profiles[p.nProfileIndex];
   p.eProfile=actual.profile;p.eLevel=actual.level;
   hidl_vec<uint8_t> result;result.resize(sizeof(p));memcpy(result.data(),&p,sizeof(p));cb(Status::OK,result);return {};
  }
  if(index==OMX_IndexParamVideoAndroidRequiresSwRenderer){
   OMX_PARAM_U32TYPE p{};
   if(input.size()!=sizeof(p)){cb(Status::BAD_VALUE,{});return {};}
   memcpy(&p,input.data(),sizeof(p));
   if(p.nSize!=sizeof(p)||p.nPortIndex!=1){cb(Status::BAD_VALUE,{});return {};}
   // Normal builds keep software output; native Surface mode is an explicit isolated candidate.
   p.nU32=nativeSurfaceCapable?0:1;hidl_vec<uint8_t> result;result.resize(sizeof(p));memcpy(result.data(),&p,sizeof(p));cb(Status::OK,result);return {};
  }
  if(index==OMX_IndexParamVideoPortFormat&&input.size()==sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE)){
   OMX_VIDEO_PARAM_PORTFORMATTYPE p;memcpy(&p,input.data(),sizeof(p));
   if(p.nPortIndex==1){
    if(p.nSize!=sizeof(p)||p.nIndex>(caps_.p010?1u:0u)){cb(Status::ERROR_UNSUPPORTED,{});return {};}
    uint32_t requested=p.nIndex;
    p.nIndex=0;
    hidl_vec<uint8_t> query;query.setToExternal(reinterpret_cast<uint8_t*>(&p),sizeof(p));
    return base_->getParameter(index,query,[cb,requested,nativeSurface](Status status,const hidl_vec<uint8_t>& output){
     if(status!=Status::OK||output.size()!=sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE)){cb(status,output);return;}
     OMX_VIDEO_PARAM_PORTFORMATTYPE p;memcpy(&p,output.data(),sizeof(p));
     p.nIndex=requested;
     p.eColorFormat=static_cast<OMX_COLOR_FORMATTYPE>(nativeSurface?(requested?QUEST2_COLOR_P010_VENUS:QUEST2_COLOR_NV12_VENUS):requested?QUEST2_COLOR_P010:OMX_COLOR_FormatYUV420SemiPlanar);
     hidl_vec<uint8_t> result;result.resize(sizeof(p));memcpy(result.data(),&p,sizeof(p));cb(status,result);
    });
   }
  }
  if(index!=OMX_IndexParamPortDefinition||input.size()!=sizeof(quest2_port32))
   return base_->getParameter(index,input,cb);
  quest2_port32 wire;memcpy(&wire,input.data(),sizeof(wire));OMX_PARAM_PORTDEFINITIONTYPE native;
  if(!quest2_port_to_native(&wire,&native,0)){cb(Status::BAD_VALUE,{});return {};}
  hidl_vec<uint8_t> bytes;bytes.setToExternal(reinterpret_cast<uint8_t*>(&native),sizeof(native));
  return base_->getParameter(index,bytes,[&](Status status,const hidl_vec<uint8_t>& output){
   hidl_vec<uint8_t> result;
   if(status==Status::OK){
    if(output.size()!=sizeof(native)){cb(Status::BAD_VALUE,{});return;}
    memcpy(&native,output.data(),sizeof(native));
    if(!quest2_port_to_wire(&native,&wire)){cb(Status::BAD_VALUE,{});return;}
    if(!nativeSurface&&!quest2_port_linear_layout(&wire)){cb(Status::BAD_VALUE,{});return;}
    result.resize(sizeof(wire));memcpy(result.data(),&wire,sizeof(wire));
   }
   cb(status,result);
  });
 }
 Return<Status> setParameter(uint32_t index,const hidl_vec<uint8_t>& input) override {
#ifdef QUEST2_NATIVE_SURFACE_TEST
  std::lock_guard<std::mutex> lock(stateMutex_);
  if(!token_||failed_)return Status::DEAD_OBJECT;
  const bool nativeSurface=outputMode_==PortMode::PRESET_ANW_BUFFER;
  if(index==consumerUsageIndex){
   OMX_PARAM_U32TYPE p{};
   if(!consumerParameter(input,&p))return Status::BAD_VALUE;
   if(!nativeSurface)return Status::INVALID_OPERATION;
   if(!outputBuffers_.empty()&&(!consumerValid_||consumerUsage_!=p.nU32))
    return Status::INVALID_OPERATION;
   Status status=configureLocked(outputMode_,p.nU32,true,producerUsage_,producerValid_);
   if(status!=Status::OK)return status;
   consumerUsage_=p.nU32;consumerValid_=true;return Status::OK;
  }
#else
  const bool nativeSurface=false;
#endif
  if(index==OMX_IndexParamVideoPortFormat&&input.size()==sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE)){
   OMX_VIDEO_PARAM_PORTFORMATTYPE p;memcpy(&p,input.data(),sizeof(p));
   if(p.nPortIndex==1){
    if(p.nSize!=sizeof(p))return Status::BAD_VALUE;
    if(nativeSurface) {
     if(caps_.p010&&static_cast<uint32_t>(p.eColorFormat)==QUEST2_COLOR_P010)
     p.eColorFormat=static_cast<OMX_COLOR_FORMATTYPE>(QUEST2_COLOR_P010_VENUS);
    const uint32_t color=p.eColorFormat;
    if(color!=QUEST2_COLOR_NV12_VENUS&&(!caps_.p010||color!=QUEST2_COLOR_P010_VENUS))return Status::ERROR_UNSUPPORTED;
    }
    else if(p.eColorFormat==OMX_COLOR_FormatYUV420SemiPlanar)
     p.eColorFormat=static_cast<OMX_COLOR_FORMATTYPE>(QUEST2_COLOR_NV12_VENUS);
    else if(caps_.p010&&static_cast<uint32_t>(p.eColorFormat)==QUEST2_COLOR_P010)
     p.eColorFormat=static_cast<OMX_COLOR_FORMATTYPE>(QUEST2_COLOR_P010_VENUS);
    else return Status::ERROR_UNSUPPORTED;
    hidl_vec<uint8_t> bytes;bytes.setToExternal(reinterpret_cast<uint8_t*>(&p),sizeof(p));
    return base_->setParameter(index,bytes);
   }
  }
  if(index!=OMX_IndexParamPortDefinition||input.size()!=sizeof(quest2_port32))
   return base_->setParameter(index,input);
  quest2_port32 wire;memcpy(&wire,input.data(),sizeof(wire));OMX_PARAM_PORTDEFINITIONTYPE native;
  if(!quest2_port_to_native(&wire,&native,1))return Status::BAD_VALUE;
  if(native.nPortIndex==1){
   if(nativeSurface) {
    if(caps_.p010&&static_cast<uint32_t>(native.format.video.eColorFormat)==QUEST2_COLOR_P010)
    native.format.video.eColorFormat=static_cast<OMX_COLOR_FORMATTYPE>(QUEST2_COLOR_P010_VENUS);
   const uint32_t color=native.format.video.eColorFormat;
   if(color!=QUEST2_COLOR_NV12_VENUS&&(!caps_.p010||color!=QUEST2_COLOR_P010_VENUS))return Status::ERROR_UNSUPPORTED;
   }
   else if(native.format.video.eColorFormat==OMX_COLOR_FormatYUV420SemiPlanar)
    native.format.video.eColorFormat=static_cast<OMX_COLOR_FORMATTYPE>(QUEST2_COLOR_NV12_VENUS);
   else if(caps_.p010&&static_cast<uint32_t>(native.format.video.eColorFormat)==QUEST2_COLOR_P010)
    native.format.video.eColorFormat=static_cast<OMX_COLOR_FORMATTYPE>(QUEST2_COLOR_P010_VENUS);
   else return Status::ERROR_UNSUPPORTED;
  }
  hidl_vec<uint8_t> bytes;bytes.setToExternal(reinterpret_cast<uint8_t*>(&native),sizeof(native));
  return base_->setParameter(index,bytes);
 }
 Return<Status> freeNode() override {
#ifdef QUEST2_NATIVE_SURFACE_TEST
  std::lock_guard<std::mutex> lock(stateMutex_);
  auto result=base_->freeNode();
  if(result.isOk()&&Status(result)==Status::OK){token_=0;outputBuffers_.clear();}
  return result;
#else
  return base_->freeNode();
#endif
 }
 ::android::hardware::Return<::android::hardware::media::omx::V1_0::Status> sendCommand(uint32_t cmd, int32_t param) override {return base_->sendCommand(cmd,param);}
 ::android::hardware::Return<void> getConfig(uint32_t index, const ::android::hardware::hidl_vec<uint8_t>& inConfig, getConfig_cb _hidl_cb) override {
#ifdef QUEST2_COLOR_TRACE
  auto dump=[index](const char *phase,const hidl_vec<uint8_t> &bytes) {
   static unsigned count;
   if(bytes.size()!=44 || __atomic_fetch_add(&count,1,__ATOMIC_RELAXED)>=128)return;
   uint32_t words[11];memcpy(words,bytes.data(),sizeof(words));
   __android_log_print(ANDROID_LOG_INFO,"Q2COLOR",
    "GET_CONFIG phase=%s index=%x size=%u ver=%x port=%u request=%u changed=%u pixel=%x space=%x range=%u primary=%u transfer=%u matrix=%u",
    phase,index,words[0],words[1],words[2],words[3],words[4],words[5],words[6],words[7],words[8],words[9],words[10]);
  };
  dump("input",inConfig);
  return base_->getConfig(index,inConfig,[dump,_hidl_cb](Status status,const hidl_vec<uint8_t> &bytes) {
   if(bytes.size()==44)__android_log_print(ANDROID_LOG_INFO,"Q2COLOR","GET_CONFIG_STATUS status=%d",int(status));
   dump("output",bytes);_hidl_cb(status,bytes);
  });
#else
  return base_->getConfig(index,inConfig,_hidl_cb);
#endif
 }
 ::android::hardware::Return<::android::hardware::media::omx::V1_0::Status> setConfig(uint32_t index, const ::android::hardware::hidl_vec<uint8_t>& config) override {return base_->setConfig(index,config);}
 Return<Status> setPortMode(uint32_t portIndex,PortMode mode) override {
#ifdef QUEST2_NATIVE_SURFACE_TEST
  if(portIndex==1){
   std::lock_guard<std::mutex> lock(stateMutex_);
   if(mode!=PortMode::PRESET_ANW_BUFFER&&mode!=PortMode::PRESET_BYTE_BUFFER)
    return Status::ERROR_UNSUPPORTED;
   if(!outputBuffers_.empty()&&mode!=outputMode_)return Status::INVALID_OPERATION;
   Status status=configureLocked(mode,consumerUsage_,consumerValid_,producerUsage_,producerValid_);
   if(status!=Status::OK)return status;
   auto result=base_->setPortMode(portIndex,mode);
   if(result.isOk()&&Status(result)==Status::OK)outputMode_=mode;
   else if(configureLocked(outputMode_,consumerUsage_,consumerValid_,producerUsage_,producerValid_)!=Status::OK)
    failed_=true;
   Q2_NODE_LOG("mode token=%llu port=%u mode=%d status=%d failed=%d",
     (unsigned long long)token_,portIndex,int(mode),result.isOk()?int(Status(result)):-1,failed_);
   return result;
  }
#endif
  return base_->setPortMode(portIndex,mode);
 }
 ::android::hardware::Return<::android::hardware::media::omx::V1_0::Status> prepareForAdaptivePlayback(uint32_t portIndex, bool enable, uint32_t maxFrameWidth, uint32_t maxFrameHeight) override {return base_->prepareForAdaptivePlayback(portIndex,enable,maxFrameWidth,maxFrameHeight);}
 ::android::hardware::Return<void> configureVideoTunnelMode(uint32_t portIndex, bool tunneled, uint32_t audioHwSync, configureVideoTunnelMode_cb _hidl_cb) override {return base_->configureVideoTunnelMode(portIndex,tunneled,audioHwSync,_hidl_cb);}
 Return<void> getGraphicBufferUsage(uint32_t portIndex,getGraphicBufferUsage_cb cb) override {
#ifdef QUEST2_NATIVE_SURFACE_TEST
  if(portIndex==1){
   Status status=Status::UNKNOWN_ERROR;uint32_t usage=0;bool called=false;
   {std::lock_guard<std::mutex> lock(stateMutex_);
    auto result=base_->getGraphicBufferUsage(portIndex,[&](Status s,uint32_t u){called=true;status=s;usage=u;});
    if(!result.isOk()||!called)status=Status::UNKNOWN_ERROR;
    if(status==Status::OK){
     if(!outputBuffers_.empty()&&(!producerValid_||usage!=producerUsage_))status=Status::INVALID_OPERATION;
     else status=configureLocked(outputMode_,consumerUsage_,consumerValid_,usage,true);
     if(status==Status::OK){producerUsage_=usage;producerValid_=true;}
    }
   }
   // Return the actual OEM requirement, never masquerade the union as OEM data.
   cb(status,status==Status::OK?usage:0);return {};
  }
#endif
  return base_->getGraphicBufferUsage(portIndex,cb);
 }
 ::android::hardware::Return<::android::hardware::media::omx::V1_0::Status> setInputSurface(const ::android::sp<::android::hardware::media::omx::V1_0::IOmxBufferSource>& bufferSource) override {return base_->setInputSurface(bufferSource);}
 ::android::hardware::Return<void> allocateSecureBuffer(uint32_t portIndex, uint64_t size, allocateSecureBuffer_cb _hidl_cb) override {return base_->allocateSecureBuffer(portIndex,size,_hidl_cb);}
 Return<void> useBuffer(uint32_t portIndex,const CodecBuffer& buffer,useBuffer_cb cb) override {
#ifdef QUEST2_NATIVE_SURFACE_TEST
  if(portIndex==1){
   Status status=Status::OK;uint32_t id=0;bool called=false;
   {std::lock_guard<std::mutex> lock(stateMutex_);
    if(!token_||failed_)status=Status::DEAD_OBJECT;
    else if(outputMode_==PortMode::PRESET_ANW_BUFFER)status=validateNativeLocked(buffer);
    else if(buffer.type!=CodecBuffer::Type::SHARED_MEM)status=Status::BAD_VALUE;
    if(status!=Status::OK)Q2_NODE_LOG("useBuffer validation token=%llu type=%u status=%d",
     (unsigned long long)token_,unsigned(buffer.type),int(status));
    if(status==Status::OK){
     auto result=base_->useBuffer(portIndex,buffer,[&](Status s,uint32_t value){called=true;status=s;id=value;});
     if(!result.isOk()||!called)status=Status::UNKNOWN_ERROR;
     if(status==Status::OK&&(!id||!outputBuffers_.insert(id).second)){status=Status::UNKNOWN_ERROR;failed_=true;}
    }
   }
   cb(status,status==Status::OK?id:0);return {};
  }
#endif
  return base_->useBuffer(portIndex,buffer,cb);
 }
 Return<Status> freeBuffer(uint32_t portIndex,uint32_t buffer) override {
#ifdef QUEST2_NATIVE_SURFACE_TEST
  if(portIndex==1){
   std::lock_guard<std::mutex> lock(stateMutex_);
   auto result=base_->freeBuffer(portIndex,buffer);
   if(result.isOk()&&Status(result)==Status::OK)outputBuffers_.erase(buffer);
   return result;
  }
#endif
  return base_->freeBuffer(portIndex,buffer);
 }
 ::android::hardware::Return<::android::hardware::media::omx::V1_0::Status> fillBuffer(uint32_t buffer, const ::android::hardware::media::omx::V1_0::CodecBuffer& omxBuffer, const ::android::hardware::hidl_handle& fence) override {return base_->fillBuffer(buffer,omxBuffer,fence);}
 ::android::hardware::Return<::android::hardware::media::omx::V1_0::Status> emptyBuffer(uint32_t buffer, const ::android::hardware::media::omx::V1_0::CodecBuffer& omxBuffer, uint32_t flags, uint64_t timestampUs, const ::android::hardware::hidl_handle& fence) override {return base_->emptyBuffer(buffer,omxBuffer,flags,timestampUs,fence);}
 Return<void> getExtensionIndex(const android::hardware::hidl_string& name,getExtensionIndex_cb cb) override {
#ifdef QUEST2_NATIVE_SURFACE_TEST
  // This wrapper implements the named extension; the unchanged donor does not.
  if(name==consumerUsageName){cb(Status::OK,consumerUsageIndex);return {};}
#endif
  return base_->getExtensionIndex(name,cb);
 }
 ::android::hardware::Return<::android::hardware::media::omx::V1_0::Status> dispatchMessage(const ::android::hardware::media::omx::V1_0::Message& msg) override {return base_->dispatchMessage(msg);}
};
class Quest2Omx final : public IOmx {
 sp<IOmx> base_;
public:
 explicit Quest2Omx(sp<IOmx> base):base_(std::move(base)){}
 Return<void> listNodes(listNodes_cb cb) override{
  return base_->listNodes([cb](Status status,const hidl_vec<IOmx::ComponentInfo>& nodes){
   hidl_vec<IOmx::ComponentInfo> supported;
   for(size_t i=0;i<nodes.size();++i)if(codecCaps.count(nodes[i].mName.c_str())){size_t n=supported.size();supported.resize(n+1);supported[n]=nodes[i];}
   cb(status,supported);
  });
 }
 Return<void> allocateNode(const android::hardware::hidl_string& name,const sp<IOmxObserver>& observer,allocateNode_cb cb) override {
  auto found=codecCaps.find(name.c_str());
  if(found==codecCaps.end()){cb(Status::NAME_NOT_FOUND,nullptr);return {};}
  const Quest2CodecCaps* caps=&found->second;
#ifdef QUEST2_NATIVE_SURFACE_TEST
  const auto& bridge=nodeBridge();
  if(base_->isRemote()||!bridge.valid()){cb(Status::ERROR_UNSUPPORTED,nullptr);return {};}
  const uint64_t token=bridge.begin();
  if(!token){cb(bridgeStatus(errno),nullptr);return {};}
  Status status=Status::UNKNOWN_ERROR;sp<IOmxNode> node;unsigned callbacks=0;
  // HIDL_FETCH is local and synchronous. End the TLS scope before notifying
  // the client, so its re-entry cannot capture opens under this node's token.
  auto result=base_->allocateNode(name,observer,[&](Status s,const sp<IOmxNode>& n){
   callbacks++;status=s;node=n;
  });
  const int ended=bridge.end(token),endError=errno;
  if(!result.isOk()||callbacks!=1)status=Status::UNKNOWN_ERROR;
  else if(status==Status::OK&&ended)status=bridgeStatus(endError);
  if(status==Status::OK&&(!node||node->isRemote()))status=Status::UNKNOWN_ERROR;
  if(status==Status::OK&&bridge.configure(token,0,0,0))status=bridgeStatus(errno);
  Q2_NODE_LOG("allocate name=%s token=%llu callbacks=%u end_error=%d status=%d",
   name.c_str(),(unsigned long long)token,callbacks,ended?endError:0,int(status));
  if(status!=Status::OK){
   if(node){auto released=node->freeNode();
    if(!released.isOk()||Status(released)!=Status::OK)Q2_NODE_LOG("failed allocation cleanup token=%llu",(unsigned long long)token);}

   cb(status,nullptr);return {};
  }
  sp<IOmxNode> wrapped=new Quest2Node(node,*caps,token);cb(Status::OK,wrapped);return {};
#else
  return base_->allocateNode(name,observer,[cb,caps](Status status,const sp<IOmxNode>& node){
   sp<IOmxNode> wrapped=node?new Quest2Node(node,*caps):nullptr;cb(status,wrapped);
  });
#endif
 }
 Return<void> createInputSurface(createInputSurface_cb cb) override{return base_->createInputSurface(cb);}
};

int main(int argc,char** argv) {
    try {
      for(auto kind:{Quest2CodecCaps::Avc,Quest2CodecCaps::Hevc,Quest2CodecCaps::Vp8,Quest2CodecCaps::Vp9,Quest2CodecCaps::Mpeg2}) {
        auto caps=Quest2CodecCaps::read(kind);
        // shortcut: native P010 remains a candidate; enable 10-bit profiles
        // after same-stream HDR reset and normal joint acceptance are verified.
        std::vector<Quest2CodecCaps::Profile> usable;
        for(const auto& p:caps.profiles)if(!caps.tenBit(p.profile))usable.push_back(p);
        caps.profiles=std::move(usable);
        if(!caps.profiles.empty())codecCaps.emplace(caps.name(),std::move(caps));
      }
    }
    catch(const std::exception& e){fprintf(stderr,"Decoder capabilities: %s\n",e.what());return 6;}
    if(argc==2&&!strcmp(argv[1],"--print-capabilities-xml")){
        try {
          fputs("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<MediaCodecs><Decoders>\n",stdout);
          for(const auto& c:codecCaps)fputs(c.second.xmlEntry().c_str(),stdout);
          fputs("</Decoders></MediaCodecs>\n",stdout);return ferror(stdout)?7:0;
        }
        catch(const std::exception& e){fprintf(stderr,"Decoder XML: %s\n",e.what());return 7;}
    }
    if(argc!=1)return 64;
    setvbuf(stdout, nullptr, _IONBF, 0);
    android::hardware::configureRpcThreadpool(8, true);
    android::sp<android::hidl::allocator::V1_0::implementation::AshmemAllocator> allocator =
        new android::hidl::allocator::V1_0::implementation::AshmemAllocator();
    if (allocator->registerAsService("ashmem")) return 5;
    android::sp<IOmx> native = HIDL_FETCH_IOmx("default");
    android::sp<IOmx> omx = native ? new Quest2Omx(native) : nullptr;
    if (!omx) return 1;
    omx->listNodes([](auto status, const auto &nodes) {
        printf("OMX_NODES status=%d count=%zu\n", int(status), nodes.size());
        for (const auto &node : nodes) printf("OMX_NODE %s\n", node.mName.c_str());
    });
    auto status = omx->registerAsService("default");
    if (status) { fprintf(stderr, "IOmx registration: %d\n", status); return 2; }
    android::sp<OmxStore> store = new OmxStore(omx, "default",
        {"/vendor/etc/sq2-omx"}, {"media_codecs.xml"}, nullptr);
    status = store->registerAsService("default");
    if (status) { fprintf(stderr, "IOmxStore registration: %d\n", status); return 3; }
    puts("QUEST2_OMX_REGISTERED");
    android::hardware::joinRpcThreadpool();
    return 4;
}
