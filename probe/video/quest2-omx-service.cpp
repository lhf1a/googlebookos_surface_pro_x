// Platform HIDL transport and XML parser, unmodified OEM OMX plugin.
#include <hidl/HidlTransportSupport.h>
#include <media/stagefright/omx/1.0/OmxStore.h>
#include <cstdio>
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
class Quest2Node final : public IOmxNode {
 sp<IOmxNode> base_;
 const Quest2CodecCaps& caps_;
public:
 explicit Quest2Node(sp<IOmxNode> base,const Quest2CodecCaps& caps):base_(std::move(base)),caps_(caps){}
 Return<void> getParameter(uint32_t index,const hidl_vec<uint8_t>& input,getParameter_cb cb) override {
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
   // The port supplies CPU-readable linear buffers; no QTI gralloc handle is exposed.
   p.nU32=1;hidl_vec<uint8_t> result;result.resize(sizeof(p));memcpy(result.data(),&p,sizeof(p));cb(Status::OK,result);return {};
  }
  if(index==OMX_IndexParamVideoPortFormat&&input.size()==sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE)){
   OMX_VIDEO_PARAM_PORTFORMATTYPE p;memcpy(&p,input.data(),sizeof(p));
   if(p.nPortIndex==1){
    if(p.nSize!=sizeof(p)||p.nIndex>(caps_.p010?1u:0u)){cb(Status::ERROR_UNSUPPORTED,{});return {};}
    uint32_t requested=p.nIndex;
    p.nIndex=0;
    hidl_vec<uint8_t> query;query.setToExternal(reinterpret_cast<uint8_t*>(&p),sizeof(p));
    return base_->getParameter(index,query,[cb,requested](Status status,const hidl_vec<uint8_t>& output){
     if(status!=Status::OK||output.size()!=sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE)){cb(status,output);return;}
     OMX_VIDEO_PARAM_PORTFORMATTYPE p;memcpy(&p,output.data(),sizeof(p));
     p.nIndex=requested;
     p.eColorFormat=static_cast<OMX_COLOR_FORMATTYPE>(requested?QUEST2_COLOR_P010:OMX_COLOR_FormatYUV420SemiPlanar);
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
    if(!quest2_port_linear_layout(&wire)){cb(Status::BAD_VALUE,{});return;}
    result.resize(sizeof(wire));memcpy(result.data(),&wire,sizeof(wire));
   }
   cb(status,result);
  });
 }
 Return<Status> setParameter(uint32_t index,const hidl_vec<uint8_t>& input) override {
  if(index==OMX_IndexParamVideoPortFormat&&input.size()==sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE)){
   OMX_VIDEO_PARAM_PORTFORMATTYPE p;memcpy(&p,input.data(),sizeof(p));
   if(p.nPortIndex==1){
    if(p.nSize!=sizeof(p))return Status::BAD_VALUE;
    if(p.eColorFormat==OMX_COLOR_FormatYUV420SemiPlanar)
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
   if(native.format.video.eColorFormat==OMX_COLOR_FormatYUV420SemiPlanar)
    native.format.video.eColorFormat=static_cast<OMX_COLOR_FORMATTYPE>(QUEST2_COLOR_NV12_VENUS);
   else if(caps_.p010&&static_cast<uint32_t>(native.format.video.eColorFormat)==QUEST2_COLOR_P010)
    native.format.video.eColorFormat=static_cast<OMX_COLOR_FORMATTYPE>(QUEST2_COLOR_P010_VENUS);
   else return Status::ERROR_UNSUPPORTED;
  }
  hidl_vec<uint8_t> bytes;bytes.setToExternal(reinterpret_cast<uint8_t*>(&native),sizeof(native));
  return base_->setParameter(index,bytes);
 }
 ::android::hardware::Return<::android::hardware::media::omx::V1_0::Status> freeNode() override {return base_->freeNode();}
 ::android::hardware::Return<::android::hardware::media::omx::V1_0::Status> sendCommand(uint32_t cmd, int32_t param) override {return base_->sendCommand(cmd,param);}
 ::android::hardware::Return<void> getConfig(uint32_t index, const ::android::hardware::hidl_vec<uint8_t>& inConfig, getConfig_cb _hidl_cb) override {return base_->getConfig(index,inConfig,_hidl_cb);}
 ::android::hardware::Return<::android::hardware::media::omx::V1_0::Status> setConfig(uint32_t index, const ::android::hardware::hidl_vec<uint8_t>& config) override {return base_->setConfig(index,config);}
 ::android::hardware::Return<::android::hardware::media::omx::V1_0::Status> setPortMode(uint32_t portIndex, ::android::hardware::media::omx::V1_0::PortMode mode) override {return base_->setPortMode(portIndex,mode);}
 ::android::hardware::Return<::android::hardware::media::omx::V1_0::Status> prepareForAdaptivePlayback(uint32_t portIndex, bool enable, uint32_t maxFrameWidth, uint32_t maxFrameHeight) override {return base_->prepareForAdaptivePlayback(portIndex,enable,maxFrameWidth,maxFrameHeight);}
 ::android::hardware::Return<void> configureVideoTunnelMode(uint32_t portIndex, bool tunneled, uint32_t audioHwSync, configureVideoTunnelMode_cb _hidl_cb) override {return base_->configureVideoTunnelMode(portIndex,tunneled,audioHwSync,_hidl_cb);}
 ::android::hardware::Return<void> getGraphicBufferUsage(uint32_t portIndex, getGraphicBufferUsage_cb _hidl_cb) override {return base_->getGraphicBufferUsage(portIndex,_hidl_cb);}
 ::android::hardware::Return<::android::hardware::media::omx::V1_0::Status> setInputSurface(const ::android::sp<::android::hardware::media::omx::V1_0::IOmxBufferSource>& bufferSource) override {return base_->setInputSurface(bufferSource);}
 ::android::hardware::Return<void> allocateSecureBuffer(uint32_t portIndex, uint64_t size, allocateSecureBuffer_cb _hidl_cb) override {return base_->allocateSecureBuffer(portIndex,size,_hidl_cb);}
 ::android::hardware::Return<void> useBuffer(uint32_t portIndex, const ::android::hardware::media::omx::V1_0::CodecBuffer& omxBuffer, useBuffer_cb _hidl_cb) override {return base_->useBuffer(portIndex,omxBuffer,_hidl_cb);}
 ::android::hardware::Return<::android::hardware::media::omx::V1_0::Status> freeBuffer(uint32_t portIndex, uint32_t buffer) override {return base_->freeBuffer(portIndex,buffer);}
 ::android::hardware::Return<::android::hardware::media::omx::V1_0::Status> fillBuffer(uint32_t buffer, const ::android::hardware::media::omx::V1_0::CodecBuffer& omxBuffer, const ::android::hardware::hidl_handle& fence) override {return base_->fillBuffer(buffer,omxBuffer,fence);}
 ::android::hardware::Return<::android::hardware::media::omx::V1_0::Status> emptyBuffer(uint32_t buffer, const ::android::hardware::media::omx::V1_0::CodecBuffer& omxBuffer, uint32_t flags, uint64_t timestampUs, const ::android::hardware::hidl_handle& fence) override {return base_->emptyBuffer(buffer,omxBuffer,flags,timestampUs,fence);}
 ::android::hardware::Return<void> getExtensionIndex(const ::android::hardware::hidl_string& parameterName, getExtensionIndex_cb _hidl_cb) override {return base_->getExtensionIndex(parameterName,_hidl_cb);}
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
  return base_->allocateNode(name,observer,[cb,caps](Status status,const sp<IOmxNode>& node){
   sp<IOmxNode> wrapped=node?new Quest2Node(node,*caps):nullptr;cb(status,wrapped);
  });
 }
 Return<void> createInputSurface(createInputSurface_cb cb) override{return base_->createInputSurface(cb);}
};

int main(int argc,char** argv) {
    try {
      for(auto kind:{Quest2CodecCaps::Avc,Quest2CodecCaps::Hevc,Quest2CodecCaps::Vp8,Quest2CodecCaps::Vp9,Quest2CodecCaps::Mpeg2}) {
        auto caps=Quest2CodecCaps::read(kind);
        // shortcut: only NV12 transport is integrated; enable 10-bit profiles
        // when the paired P010 buffer and HDR display paths are connected.
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
