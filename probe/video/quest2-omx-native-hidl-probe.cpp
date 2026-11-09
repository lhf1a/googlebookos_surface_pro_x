/* Real remote HIDL native-buffer lifecycle and optional AVC frame diagnostic. */
#include <hidl/HidlTransportSupport.h>
#include <android/hardware/media/omx/1.0/IOmx.h>
#include <cassert>
#include <condition_variable>
#include <chrono>
#include <mutex>
#include <vector>
#include <deque>
#include <set>
#include <algorithm>
#include <sys/mman.h>
#include <poll.h>
#include <cerrno>
#include <unistd.h>
#include "sq2-h264-vector.h"
#include <android/hidl/allocator/1.0/IAllocator.h>
#include <cstdio>
#include "quest2-omx-port-abi.h"
#include <OMX_IndexExt.h>
#include "../graphics/quest2-mapper5-video-buffer.h"
using namespace android::hardware::media::omx::V1_0;
using android::hardware::Return;
using android::hardware::hidl_vec;
using android::sp;
struct Observer final:IOmxObserver {
 std::mutex mutex;std::condition_variable condition;unsigned state=OMX_StateLoaded;bool error=false;
 std::deque<Message> pending;
 std::set<std::pair<unsigned,unsigned>> completed;
 Return<void> onMessages(const hidl_vec<Message>& messages) override {
  std::lock_guard<std::mutex> lock(mutex);
  for(const auto& m:messages){
   pending.push_back(m); // HIDL copies and owns callback fence handles beyond this call.
   if(m.type==Message::Type::EVENT){
   printf("HIDL_NATIVE_EVENT type=%u data1=%u data2=%u\n",m.data.eventData.event,m.data.eventData.data1,m.data.eventData.data2);
   if(m.data.eventData.event==OMX_EventError)error=true;
   if(m.data.eventData.event==OMX_EventCmdComplete) {
    completed.emplace(m.data.eventData.data1,m.data.eventData.data2);
    if(m.data.eventData.data1==OMX_CommandStateSet)state=m.data.eventData.data2;
   }
  }}
  condition.notify_all();return {};
 }
 bool commandDone(unsigned command,unsigned argument) {
  std::lock_guard<std::mutex> lock(mutex);assert(!error);
  return completed.erase({command,argument})!=0;
 }
 std::deque<Message> take() {
  std::unique_lock<std::mutex> lock(mutex);
  condition.wait_for(lock,std::chrono::milliseconds(100),[&]{return error||!pending.empty();});
  assert(!error);std::deque<Message> result;result.swap(pending);return result;
 }
 bool wait(unsigned wanted) {
  std::unique_lock<std::mutex> lock(mutex);
  return condition.wait_for(lock,std::chrono::seconds(5),[&]{return error||state==wanted;})&&!error&&state==wanted;
 }
};
template<class T> static void query(const sp<IOmxNode>& node,uint32_t index,T& value) {
 hidl_vec<uint8_t> in;in.setToExternal(reinterpret_cast<uint8_t*>(&value),sizeof(value));
 Status status=Status::UNKNOWN_ERROR;
 auto result=node->getParameter(index,in,[&](Status s,const hidl_vec<uint8_t>& out){
  status=s;if(s==Status::OK){assert(out.size()==sizeof(value));memcpy(&value,out.data(),out.size());}
 });
 assert(result.isOk()&&status==Status::OK);
}
static void waitFence(const android::hardware::hidl_handle& handle) {
 const native_handle_t *f=handle.getNativeHandle();
 if(!f)return;
 assert(f->numInts==0&&(f->numFds==0||f->numFds==1));
 if(!f->numFds)return; // HIDL also represents NO_FENCE with an empty handle.
 pollfd p{f->data[0],POLLIN,0};int rc;
 do {rc=poll(&p,1,5000);} while(rc<0&&errno==EINTR);
 assert(rc==1&&(p.revents&POLLIN)&&!(p.revents&(POLLERR|POLLNVAL)));
}
static unsigned decode(const sp<IOmxNode>& node,const sp<Observer>& observer,
 const std::vector<android::hardware::hidl_memory>& memory,const std::vector<uint32_t>& inputs,
 std::vector<uint32_t>& outputs,std::vector<quest2_video_buffer>& buffers,
 quest2_port32& port,uint32_t usage,const char *directory) {
 std::vector<void *> mapped(memory.size());
 for(size_t i=0;i<memory.size();i++){
  const native_handle_t *h=memory[i].handle();
  assert(h&&h->numFds==1&&memory[i].size()>0);
  mapped[i]=mmap(nullptr,memory[i].size(),PROT_READ|PROT_WRITE,MAP_SHARED,h->data[0],0);
  assert(mapped[i]!=MAP_FAILED);
 }
 std::vector<bool> inputBusy(inputs.size()),outputBusy(outputs.size());
 unsigned sent=0,frames=0,reconfigurations=0;
 std::set<uint64_t> timestamps;bool ordered=true;
 bool eos=false,disabling=false;
 CodecBuffer preset{};preset.type=CodecBuffer::Type::PRESET;
 auto fill=[&](size_t i){
  assert(!outputBusy[i]&&outputs[i]);
  assert(node->fillBuffer(outputs[i],preset,{})==Status::OK);outputBusy[i]=true;
 };
 auto registerOutputs=[&]{
  assert(port.actual>=port.minimum&&port.actual>0&&port.actual<=32);
  outputs.assign(port.actual,0);buffers.resize(port.actual);outputBusy.assign(port.actual,false);
  for(size_t i=0;i<outputs.size();i++){
   auto& b=buffers[i];
   assert(!quest2_video_buffer_allocate(port.video.width,port.video.height,port.video.color,usage|0x400b03ULL,&b));
   CodecBuffer c{};c.type=CodecBuffer::Type::ANW_BUFFER;
   c.nativeHandle.setTo(const_cast<native_handle_t *>(b.handle),false);
   auto& a=c.attr.anwBuffer;
   a.width=port.video.width;a.height=port.video.height;a.stride=b.stride;
   a.format=static_cast<decltype(a.format)>(port.video.color);a.usage=b.usage;a.layerCount=1;
   assert(!AHardwareBuffer_getId(b.ahb,&a.id));
   assert(node->useBuffer(1,c,[&](Status status,uint32_t id){assert(status==Status::OK);outputs[i]=id;}).isOk());
   assert(outputs[i]);
  }
 };
 auto save=[&](size_t i,const Message::ExtendedBufferData& d){
  assert(frames<SQ2_VEC_FRAMES&&d.timestampUs%33333==0&&d.timestampUs/33333<SQ2_VEC_FRAMES);
  assert(timestamps.insert(d.timestampUs).second);
  ordered=ordered&&(d.timestampUs==uint64_t(frames)*33333);
  OMX_CONFIG_RECTTYPE crop{};crop.nSize=sizeof(crop);crop.nVersion.nVersion=0x101;crop.nPortIndex=1;
  hidl_vec<uint8_t> bytes;bytes.setToExternal(reinterpret_cast<uint8_t*>(&crop),sizeof(crop));
  assert(node->getConfig(OMX_IndexConfigCommonOutputCrop,bytes,[&](Status st,const hidl_vec<uint8_t>& value){
   assert(st==Status::OK&&value.size()==sizeof(crop));memcpy(&crop,value.data(),value.size());
  }).isOk());
  printf("HIDL_NATIVE_CROP frame=%u x=%d y=%d w=%u h=%u\n",frames,crop.nLeft,crop.nTop,crop.nWidth,crop.nHeight);
  assert(!crop.nLeft&&!crop.nTop&&crop.nWidth==SQ2_VEC_W&&crop.nHeight==SQ2_VEC_H);
  auto& b=buffers[i];void *pixels=nullptr;
  assert(b.ystride>=SQ2_VEC_W&&b.cstride>=SQ2_VEC_W&&
         uint64_t(SQ2_VEC_H)*b.ystride<=b.uv&&
         uint64_t(b.uv)+uint64_t(SQ2_VEC_H/2-1)*b.cstride+SQ2_VEC_W<=b.capacity);
  assert(!AHardwareBuffer_lock(b.ahb,AHARDWAREBUFFER_USAGE_CPU_READ_RARELY,-1,nullptr,&pixels)&&pixels);
  char name[512];int n=snprintf(name,sizeof(name),"%s/frame-%u.nv12",directory,frames);
  assert(n>0&&size_t(n)<sizeof(name));FILE *file=fopen(name,"wbx");assert(file);
  auto *base=static_cast<const uint8_t *>(pixels);
  for(unsigned y=0;y<SQ2_VEC_H;y++)assert(fwrite(base+size_t(y)*b.ystride,1,SQ2_VEC_W,file)==SQ2_VEC_W);
  for(unsigned y=0;y<SQ2_VEC_H/2;y++)assert(fwrite(base+b.uv+size_t(y)*b.cstride,1,SQ2_VEC_W,file)==SQ2_VEC_W);
  assert(!fclose(file));assert(!AHardwareBuffer_unlock(b.ahb,nullptr));
  printf("HIDL_NATIVE_FRAME frame=%u buffer=%u pts=%llu length=%u flags=%x stride=%u uv=%u\n",
         frames,outputs[i],(unsigned long long)d.timestampUs,d.rangeLength,d.flags,b.ystride,b.uv);
  frames++;
 };
 assert(node->sendCommand(OMX_CommandStateSet,OMX_StateExecuting)==Status::OK&&observer->wait(OMX_StateExecuting));
 for(size_t i=0;i<outputs.size();i++)fill(i);
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(18);
 while(!eos&&std::chrono::steady_clock::now()<deadline){
  for(const auto& message:observer->take()){
   waitFence(message.fence);
   if(message.type==Message::Type::EVENT){
    const auto& e=message.data.eventData;
    if(e.event==OMX_EventPortSettingsChanged&&e.data1==1&&
       (!e.data2||e.data2==OMX_IndexParamPortDefinition)){
     assert(!disabling);disabling=true;
     assert(node->sendCommand(OMX_CommandPortDisable,1)==Status::OK);
    }
   }else if(message.type==Message::Type::EMPTY_BUFFER_DONE){
    auto it=std::find(inputs.begin(),inputs.end(),message.data.bufferData.buffer);assert(it!=inputs.end());
    size_t i=it-inputs.begin();assert(inputBusy[i]);inputBusy[i]=false;
   }else if(message.type==Message::Type::FILL_BUFFER_DONE){
    const auto& d=message.data.extendedBufferData;
    printf("HIDL_NATIVE_FBD buffer=%u pts=%llu length=%u flags=%x offset=%u\n",
           d.buffer,(unsigned long long)d.timestampUs,d.rangeLength,d.flags,d.rangeOffset);
    auto it=std::find(outputs.begin(),outputs.end(),d.buffer);assert(it!=outputs.end());
    size_t i=it-outputs.begin();assert(outputBusy[i]);outputBusy[i]=false;
    if(d.rangeLength)save(i,d);
    if(d.flags&OMX_BUFFERFLAG_EOS)eos=true;
   }
  }
  if(disabling){
   for(size_t i=0;i<outputs.size();i++)if(outputs[i]&&!outputBusy[i]){
    assert(node->freeBuffer(1,outputs[i])==Status::OK);outputs[i]=0;quest2_video_buffer_release(&buffers[i]);
   }
   if(observer->commandDone(OMX_CommandPortDisable,1)){
    for(auto id:outputs)assert(!id);
    query(node,OMX_IndexParamPortDefinition,port);
    hidl_vec<uint8_t> bytes;bytes.setToExternal(reinterpret_cast<uint8_t*>(&port),sizeof(port));
    assert(node->setParameter(OMX_IndexParamPortDefinition,bytes)==Status::OK);
    query(node,OMX_IndexParamPortDefinition,port);
    printf("HIDL_NATIVE_RECONFIG width=%u height=%u stride=%d scan=%u count=%u bytes=%u\n",
           port.video.width,port.video.height,port.video.stride,port.video.slice,port.actual,port.bytes);
    assert(node->sendCommand(OMX_CommandPortEnable,1)==Status::OK);
    registerOutputs();
    const auto enabled=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!observer->commandDone(OMX_CommandPortEnable,1)){
     assert(std::chrono::steady_clock::now()<enabled);usleep(10000);
    }
    disabling=false;reconfigurations++;
   }
  }
  if(!disabling&&!eos)for(size_t i=0;i<outputs.size();i++)if(!outputBusy[i])fill(i);
  for(size_t i=0;i<inputs.size()&&sent<=SQ2_VEC_FRAMES;i++)if(!inputBusy[i]){
   size_t bytes=sent<SQ2_VEC_FRAMES?sq2_vec_au[sent+1]-sq2_vec_au[sent]:0;
   assert(bytes<=memory[i].size());
   if(bytes)memcpy(mapped[i],sq2_vec_stream+sq2_vec_au[sent],bytes);
   CodecBuffer c{};c.type=CodecBuffer::Type::PRESET;c.attr.preset.rangeLength=bytes;
   assert(node->emptyBuffer(inputs[i],c,bytes?OMX_BUFFERFLAG_ENDOFFRAME:OMX_BUFFERFLAG_EOS,(sent<SQ2_VEC_FRAMES?sq2_vec_pts[sent]:uint64_t(sent)*33333),{})==Status::OK);
   printf("HIDL_NATIVE_INPUT frame=%u bytes=%zu\n",sent,bytes);inputBusy[i]=true;sent++;
  }
 }
 printf("HIDL_NATIVE_DECODE frames=%u sent=%u eos=%d reconfigurations=%u\n",frames,sent,eos,reconfigurations);
 assert(eos&&frames==SQ2_VEC_FRAMES&&!disabling);
 assert(node->sendCommand(OMX_CommandStateSet,OMX_StateIdle)==Status::OK&&observer->wait(OMX_StateIdle));
 for(const auto& message:observer->take()){
  waitFence(message.fence);
  if(message.type==Message::Type::EMPTY_BUFFER_DONE){
   auto it=std::find(inputs.begin(),inputs.end(),message.data.bufferData.buffer);assert(it!=inputs.end());
   size_t i=it-inputs.begin();assert(inputBusy[i]);inputBusy[i]=false;
  }else if(message.type==Message::Type::FILL_BUFFER_DONE){
   const auto& d=message.data.extendedBufferData;assert(!d.rangeLength);
   auto it=std::find(outputs.begin(),outputs.end(),d.buffer);assert(it!=outputs.end());
   size_t i=it-outputs.begin();assert(outputBusy[i]);outputBusy[i]=false;
  }
 }
 for(auto busy:inputBusy)assert(!busy);
 for(auto busy:outputBusy)assert(!busy);
 for(size_t i=0;i<mapped.size();i++)assert(!munmap(mapped[i],memory[i].size()));
 printf("HIDL_NATIVE_ORDER ordered=%d unique_pts=%zu\n",ordered,timestamps.size());
 assert(ordered);
 return frames;
}
int main(int argc,char **argv) {
 if(argc>2){fprintf(stderr,"usage: %s [NEW_FRAME_DIRECTORY]\n",argv[0]);return 64;}
 setvbuf(stdout,nullptr,_IONBF,0);
 android::hardware::configureRpcThreadpool(4,false);
 auto omx=IOmx::getService();assert(omx&&omx->isRemote());
 sp<IOmxNode> node;sp<Observer> observer=new Observer;
 assert(omx->allocateNode("OMX.qcom.video.decoder.avc",observer,[&](Status status,const sp<IOmxNode>& n){
  assert(status==Status::OK);node=n;
 }).isOk()&&node&&node->isRemote());
 OMX_PARAM_U32TYPE sw{};sw.nSize=sizeof(sw);sw.nVersion.nVersion=0x101;sw.nPortIndex=1;
 query(node,OMX_IndexParamVideoAndroidRequiresSwRenderer,sw);assert(!sw.nU32);
 assert(node->setPortMode(1,PortMode::DYNAMIC_ANW_BUFFER)==Status::ERROR_UNSUPPORTED);
 assert(node->setPortMode(1,PortMode::PRESET_SECURE_BUFFER)==Status::ERROR_UNSUPPORTED);
 auto mode=node->setPortMode(1,PortMode::PRESET_ANW_BUFFER);
 printf("HIDL_NATIVE_MODE status=%d\n",mode.isOk()?int(Status(mode)):-1);
 if(!mode.isOk()||Status(mode)!=Status::OK){node->freeNode();return 2;}
 OMX_VIDEO_PARAM_PORTFORMATTYPE format{};format.nSize=sizeof(format);format.nVersion.nVersion=0x101;format.nPortIndex=1;
 query(node,OMX_IndexParamVideoPortFormat,format);assert(uint32_t(format.eColorFormat)==QUEST2_COLOR_NV12_VENUS);
 hidl_vec<uint8_t> bytes;bytes.setToExternal(reinterpret_cast<uint8_t*>(&format),sizeof(format));
 assert(node->setParameter(OMX_IndexParamVideoPortFormat,bytes)==Status::OK);
 quest2_port32 port{};port.size=sizeof(port);port.version=0x101;port.port=1;
 query(node,OMX_IndexParamPortDefinition,port);
 assert(port.video.color==QUEST2_COLOR_NV12_VENUS&&port.video.width&&port.video.height);
 uint32_t usage=0;
 assert(node->getGraphicBufferUsage(1,[&](Status status,uint32_t value){assert(status==Status::OK);usage=value;}).isOk());
 assert(usage==0x20000000);
 printf("HIDL_NATIVE_PORT width=%u height=%u stride=%d scan=%u format=%x usage=%x\n",
        port.video.width,port.video.height,port.video.stride,port.video.slice,port.video.color,usage);
 // ACodec commits the negotiated output count/size before Loaded->Idle.
 bytes.setToExternal(reinterpret_cast<uint8_t*>(&port),sizeof(port));
 assert(node->setParameter(OMX_IndexParamPortDefinition,bytes)==Status::OK);
 query(node,OMX_IndexParamPortDefinition,port);
 quest2_port32 input{};input.size=sizeof(input);input.version=0x101;
 query(node,OMX_IndexParamPortDefinition,input);
 assert(input.actual>=input.minimum&&input.actual<=32&&port.actual>=port.minimum&&port.actual<=32);
 auto ashmem=android::hidl::allocator::V1_0::IAllocator::getService("ashmem");assert(ashmem&&ashmem->isRemote());
 std::vector<android::hardware::hidl_memory> memory(input.actual);
 std::vector<uint32_t> inputs,outputs;
 std::vector<quest2_video_buffer> buffers(port.actual);
 assert(node->sendCommand(OMX_CommandStateSet,OMX_StateIdle)==Status::OK);
 bool ok=true;
 for(unsigned i=0;i<input.actual&&ok;i++) {
  bool allocated=false;
  assert(ashmem->allocate(input.bytes,[&](bool success,const android::hardware::hidl_memory& m){allocated=success;memory[i]=m;}).isOk()&&allocated);
  CodecBuffer codec{};codec.type=CodecBuffer::Type::SHARED_MEM;codec.sharedMemory=memory[i];
  Status status=Status::UNKNOWN_ERROR;uint32_t id=0;
  auto result=node->useBuffer(0,codec,[&](Status s,uint32_t value){status=s;id=value;});
  ok=result.isOk()&&status==Status::OK;if(ok)inputs.push_back(id);
 }
 for(unsigned i=0;i<port.actual&&ok;i++) {
  auto& buffer=buffers[i];
  assert(!quest2_video_buffer_allocate(port.video.width,port.video.height,port.video.color,usage|0x400b03ULL,&buffer));
  CodecBuffer codec{};codec.type=CodecBuffer::Type::ANW_BUFFER;
  codec.nativeHandle.setTo(const_cast<native_handle_t *>(buffer.handle),false);
  auto& attr=codec.attr.anwBuffer;
  attr.width=port.video.width;attr.height=port.video.height;attr.stride=buffer.stride;
  attr.format=static_cast<decltype(attr.format)>(port.video.color);attr.usage=buffer.usage;attr.layerCount=1;
  assert(!AHardwareBuffer_getId(buffer.ahb,&attr.id));
  uint32_t id=0;Status status=Status::UNKNOWN_ERROR;
  auto imported=node->useBuffer(1,codec,[&](Status s,uint32_t value){status=s;id=value;});
  printf("HIDL_NATIVE_IMPORT index=%u transport=%d status=%d buffer_id=%u\n",i,imported.isOk(),int(status),id);
  ok=imported.isOk()&&status==Status::OK;if(ok)outputs.push_back(id);
 }
 if(ok)ok=observer->wait(OMX_StateIdle);
 printf("HIDL_NATIVE_IDLE success=%d input_buffers=%zu output_buffers=%zu\n",ok,inputs.size(),outputs.size());
 unsigned frames=0;
 if(ok&&argc==2)frames=decode(node,observer,memory,inputs,outputs,buffers,port,usage,argv[1]);
 if(ok)ok=node->sendCommand(OMX_CommandStateSet,OMX_StateLoaded)==Status::OK;
 for(auto id:inputs)ok=(node->freeBuffer(0,id)==Status::OK)&&ok;
 for(auto id:outputs)ok=(node->freeBuffer(1,id)==Status::OK)&&ok;
 for(auto& buffer:buffers)quest2_video_buffer_release(&buffer);
 if(ok)ok=observer->wait(OMX_StateLoaded);
 ok=(node->freeNode()==Status::OK)&&ok;
 quest2_video_buffer_check_maps();
 printf("HIDL_NATIVE_RESULT success=%d decoded_frames=%u\n",ok,frames);
 return ok?0:3;
}
