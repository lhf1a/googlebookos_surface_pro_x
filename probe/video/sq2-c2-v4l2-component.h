#pragma once
#include "sq2-p010-copy.h"
// Experimental in-process adapter. Not registered and not a complete MediaCodec implementation.
#include <android/log.h>
#define SQ2_SESSION_TRACE(stage) __android_log_print(ANDROID_LOG_INFO,"SQ2C2Lifecycle","CLOSE_STAGE %s",stage)
#define SQ2_SESSION_ERROR(error) __android_log_print(ANDROID_LOG_ERROR,"SQ2C2Lifecycle","SESSION_ERROR errno=%d",error)
#include "sq2-v4l2-session.h"
#include "sq2-avc-config.h"
#include "sq2-codecs.h"
#include "sq2-split-uv.h"
#include <SimpleC2Component.h>
#include <C2AllocatorGralloc.h>
#include <chrono>
#include <set>
#include <sys/stat.h>
#include <linux/dma-heap.h>
#include <linux/dma-buf.h>
#include <android/hardware_buffer.h>
#include <vndk/hardware_buffer.h>
#include <map>
#include <dlfcn.h>
class Sq2C2V4L2Component final:public android::SimpleC2Component {
 struct Lease {
  int fd=-1;void* mapping=MAP_FAILED;size_t bytes=0;
  std::shared_ptr<C2GraphicBlock> graphic;
  ~Lease(){if(mapping!=MAP_FAILED)munmap(mapping,bytes);if(fd>=0)::close(fd);}
  int sync(uint64_t flags){dma_buf_sync s{};s.flags=flags;int r;do{r=ioctl(fd,DMA_BUF_IOCTL_SYNC,&s);}while(r<0&&errno==EINTR);return r<0?-errno:0;}
 };
 Sq2V4L2Session session_;std::shared_ptr<C2BlockPool> pool_;const uint32_t codec_;uint32_t outputFormat_=V4L2_PIX_FMT_NV12;
 bool directP010_=false;
 Sq2AvcConfig config_,configured_;bool collectingConfig_=false;
 std::set<uint64_t> outputInodes_;
 // Import references retain allocator objects, never BufferQueue block ownership
 // or a CPU lock. Mapping cache itself remains private to the lab process.
 struct Import {std::shared_ptr<AHardwareBuffer> buffer;size_t bytes;uint64_t used;};
 std::map<std::pair<uint64_t,uint64_t>,Import> imports_;
 size_t importBytes_=0;uint64_t importClock_=0;
 bool retainImport(const C2Handle* wrapped,const native_handle_t* native,const struct stat& st){
  const char* enabled=getenv("SQ2_VIDEO_MAP_CACHE");if(!enabled||strcmp(enabled,"1"))return true;
  auto key=std::make_pair(uint64_t(st.st_dev),uint64_t(st.st_ino));
  auto existing=imports_.find(key);if(existing!=imports_.end()){existing->second.used=++importClock_;return true;}
  auto bytes=lseek(native->data[0],0,SEEK_END);if(bytes>=0)lseek(native->data[0],0,SEEK_SET);
  if(bytes<=0||bytes>16*1024*1024)return true; // Optional optimization, not a decode requirement.
  while(imports_.size()>=16||importBytes_+size_t(bytes)>192*1024*1024){
   auto oldest=imports_.begin();
   for(auto i=imports_.begin();i!=imports_.end();++i)if(i->second.used<oldest->second.used)oldest=i;
   importBytes_-=oldest->second.bytes;
   // Only our independent import reference is released. No queue slot or CPU lock is held.
   imports_.erase(oldest);
   __android_log_print(ANDROID_LOG_INFO,"SQ2C2Lifecycle","IMPORT_EVICT remaining=%zu bytes=%zu",imports_.size(),importBytes_);
  }
  AHardwareBuffer_Desc desc{};uint32_t generation=0,slot=0;uint64_t id=0;
  android::_UnwrapNativeCodec2GrallocMetadata(wrapped,&desc.width,&desc.height,&desc.format,&desc.usage,&desc.stride,&generation,&id,&slot);
  desc.layers=1;AHardwareBuffer* buffer=nullptr;
  static void* library=dlopen("libnativewindow.so",RTLD_NOW|RTLD_LOCAL);
  static auto create=library?reinterpret_cast<decltype(&AHardwareBuffer_createFromHandle)>(dlsym(library,"AHardwareBuffer_createFromHandle")):nullptr;
  if(!create||create(&desc,native,AHARDWAREBUFFER_CREATE_FROM_HANDLE_METHOD_CLONE,&buffer)||!buffer)return false;
  imports_.emplace(key,Import{std::shared_ptr<AHardwareBuffer>(buffer,AHardwareBuffer_release),size_t(bytes),++importClock_});importBytes_+=size_t(bytes);
  __android_log_print(ANDROID_LOG_INFO,"SQ2C2Lifecycle","IMPORT_KEEP count=%zu bytes=%zu",imports_.size(),importBytes_);return true;
 }
 using Clock=std::chrono::steady_clock;
 uint64_t timedFrames_=0,fetchUs_=0,mapUs_=0,copyUs_=0,unmapUs_=0,syncUs_=0,yUs_=0,uvUs_=0;
 static uint64_t micros(Clock::time_point a,Clock::time_point b){return std::chrono::duration_cast<std::chrono::microseconds>(b-a).count();}
 const std::unique_ptr<C2Work>* current_=nullptr;std::set<uint64_t> pending_;
 int allocate(const Sq2Geometry& g,Sq2CaptureBuffer& out){
  auto lease=std::make_shared<Lease>();
  if(!g.bytes||g.bytes>sq2CaptureFrameLimit(g.format))return -EINVAL;
  if(directP010_){
   if(g.format!=V4L2_PIX_FMT_P010||!pool_)return -EINVAL;
   auto status=pool_->fetchGraphicBlock(g.width,g.height,0x36,
       C2MemoryUsage{C2MemoryUsage::CPU_READ|C2MemoryUsage::CPU_WRITE},&lease->graphic);
   if(status!=C2_OK){
    __android_log_print(ANDROID_LOG_ERROR,"SQ2C2Lifecycle","DIRECT_FETCH_FAILED c2_status=%d pool=%llu capture_count=%u",int(status),(unsigned long long)pool_->getLocalId(),g.count);
    if(status==C2_BLOCKING)return -EAGAIN;
    return status==C2_NO_MEMORY?-ENOMEM:-EIO;
   }
   { // map().get() waits the acquisition fence; scoped view unlocks before QBUF.
    auto view=lease->graphic->map().get();if(view.error()!=C2_OK)return -EIO;
    auto l=view.layout();auto p=view.data();
    if(l.type!=C2PlanarLayout::TYPE_YUV||l.numPlanes!=3)return -ENOTSUP;
    for(unsigned i=0;i<3;++i){const auto& v=l.planes[i];
     if(!p[i]||v.bitDepth!=10||v.allocatedDepth!=16||v.rightShift!=6||v.endianness==C2PlaneInfo::BIG_END||
        v.rowInc!=int(g.stride)||v.colInc!=(i?4:2)||v.colSampling!=(i?2u:1u)||v.rowSampling!=(i?2u:1u))return -ENOTSUP;
    }
    if(uintptr_t(p[1])!=uintptr_t(p[0])+uint64_t(g.stride)*g.scanlines||uintptr_t(p[2])!=uintptr_t(p[1])+2||l.planes[0].offset!=0)return -ENOTSUP;
   }
   auto handle=android::UnwrapNativeCodec2GrallocHandle(lease->graphic->handle());
   if(!handle)return -EINVAL;
   struct stat y{},uv{};bool valid=handle->numFds>=2&&fstat(handle->data[0],&y)==0&&fstat(handle->data[1],&uv)==0&&y.st_dev==uv.st_dev&&y.st_ino==uv.st_ino;
   auto bytes=valid?lseek(handle->data[0],0,SEEK_END):-1;
   if(bytes>=0)lseek(handle->data[0],0,SEEK_SET);
   if(bytes<g.bytes||bytes>int64_t(sq2CaptureFrameLimit(g.format))||uint64_t(bytes)*g.count>sq2CapturePoolLimit(g.format)){free(handle);return -EINVAL;}
   lease->fd=fcntl(handle->data[0],F_DUPFD_CLOEXEC,0);free(handle);if(lease->fd<0)return -errno;
   lease->bytes=size_t(bytes);
   __android_log_print(ANDROID_LOG_INFO,"SQ2C2Lifecycle","DIRECT_P010 fd=%d bytes=%zu inode=%llu count=%u",lease->fd,lease->bytes,(unsigned long long)y.st_ino,g.count);
   out={lease->fd,lease->bytes,lease};return 0;
  }
  int heap=::open("/dev/dma_heap/system",O_RDWR|O_CLOEXEC);if(heap<0)return -errno;
  dma_heap_allocation_data data{};data.len=g.bytes;data.fd_flags=O_RDWR|O_CLOEXEC;
  int r;do{r=ioctl(heap,DMA_HEAP_IOCTL_ALLOC,&data);}while(r<0&&errno==EINTR);
  int error=r<0?-errno:0;::close(heap);if(error)return error;
  lease->fd=data.fd;lease->bytes=g.bytes;
  lease->mapping=mmap(nullptr,g.bytes,PROT_READ,MAP_SHARED,lease->fd,0);if(lease->mapping==MAP_FAILED)return -errno;
  __android_log_print(ANDROID_LOG_INFO,"SQ2C2Lifecycle","NATIVE_CAPTURE fd=%d bytes=%zu width=%u height=%u count=%u",lease->fd,lease->bytes,g.width,g.height,g.count);
  out={lease->fd,lease->bytes,lease};return 0;
 }
 void complete(uint64_t token,std::shared_ptr<C2Buffer> buffer,c2_status_t result,bool eos=false){
  auto fill=[buffer,result,eos](const std::unique_ptr<C2Work>& w){
   w->result=result;w->workletsProcessed=1;auto& output=w->worklets.front()->output;
   output.flags=eos?C2FrameData::FLAG_END_OF_STREAM:C2FrameData::flags_t(0);output.ordinal=w->input.ordinal;
   output.buffers.clear();if(buffer)output.buffers.push_back(buffer);w->input.buffers.clear();
  };
  if(current_&&(*current_)->input.ordinal.frameIndex.peeku()==token)fill(*current_);else finish(token,fill);
  pending_.erase(token);
 }
 void deliver(Sq2DecodedFrame frame){
  if(!pending_.count(frame.token))return;
  auto lease=std::static_pointer_cast<Lease>(frame.buffer.owner);
  auto& g=frame.geometry;
  const bool ten=outputFormat_==V4L2_PIX_FMT_P010;
  const unsigned sampleBytes=ten?2:1;
  uint64_t rowBytes=uint64_t(g.width)*sampleBytes;
  uint64_t uv=uint64_t(g.stride)*g.scanlines;
  if(g.format!=outputFormat_||frame.offset||!g.width||!g.height||(g.width&1)||(g.height&1)||rowBytes>g.stride||uv+uint64_t(g.stride)*(g.height/2)>lease->bytes||uv+uint64_t(g.stride)*(g.height/2-1)+rowBytes>frame.bytes){complete(frame.token,{},C2_CORRUPTED);return;}
  if(lease->graphic){
   // DQBUF completes device writes. Never requeue this lease: the session replaces
   // it, while the shared graphic block remains owned by the downstream client.
   ++timedFrames_;
   complete(frame.token,C2Buffer::CreateGraphicBuffer(lease->graphic->share(C2Rect(g.width,g.height),C2Fence())),C2_OK);
   return;
  }
  std::shared_ptr<C2GraphicBlock> output;
  auto begin=Clock::now();
  if(!pool_||pool_->fetchGraphicBlock(g.width,g.height,ten?0x36:0x23,C2MemoryUsage{C2MemoryUsage::CPU_READ|C2MemoryUsage::CPU_WRITE},&output)!=C2_OK){complete(frame.token,{},C2_NO_MEMORY);return;}
  auto fetched=Clock::now(),mapped=fetched,copied=fetched;
  auto handle=android::UnwrapNativeCodec2GrallocHandle(output->handle());
  if(handle){struct stat st{};if(handle->numFds>0&&fstat(handle->data[0],&st)==0){
   if(!retainImport(output->handle(),handle,st)){free(handle);complete(frame.token,{},C2_NO_MEMORY);return;}
   outputInodes_.insert(st.st_ino);
   if(timedFrames_<12)__android_log_print(ANDROID_LOG_INFO,"SQ2C2Lifecycle","OUTPUT_OWNER frame=%llu pool=%llu allocator=%u inode=%llu",(unsigned long long)timedFrames_,(unsigned long long)pool_->getLocalId(),pool_->getAllocatorId(),(unsigned long long)st.st_ino);
  }free(handle);}
  bool good=true;
  {
   auto view=output->map().get();if(view.error()!=C2_OK){complete(frame.token,{},C2_CORRUPTED);return;}auto layout=view.layout();auto p=view.data();
   mapped=Clock::now();
   if(!timedFrames_)__android_log_print(ANDROID_LOG_INFO,"SQ2C2Lifecycle","UV_LAYOUT delta=%lld roots=%u,%u offsets=%u,%u",(long long)(intptr_t(p[2])-intptr_t(p[1])),layout.planes[1].rootIx,layout.planes[2].rootIx,layout.planes[1].offset,layout.planes[2].offset);
   if(!timedFrames_)__android_log_print(ANDROID_LOG_INFO,"SQ2C2Lifecycle","LAYOUT rows=%d,%d,%d cols=%d,%d,%d",layout.planes[0].rowInc,layout.planes[1].rowInc,layout.planes[2].rowInc,layout.planes[0].colInc,layout.planes[1].colInc,layout.planes[2].colInc);
   good=view.error()==C2_OK&&layout.type==C2PlanarLayout::TYPE_YUV&&layout.numPlanes==3;
   for(unsigned plane=0;plane<3&&good;plane++){
    auto& l=layout.planes[plane];unsigned w=plane?g.width/2:g.width;
    good=p[plane]&&l.bitDepth==(ten?10u:8u)&&l.colInc>=int(sampleBytes)&&l.colInc<=int(2*sampleBytes)&&l.rowInc>0&&uint64_t(l.rowInc)>=uint64_t(w-1)*l.colInc+sampleBytes;
    if(ten)good=good&&l.allocatedDepth==16&&l.rightShift==6&&l.endianness!=C2PlaneInfo::BIG_END&&l.colSampling==(plane?2u:1u)&&l.rowSampling==(plane?2u:1u);
   }
   if(good){
    auto syncBegin=Clock::now();good=lease->sync(DMA_BUF_SYNC_START|DMA_BUF_SYNC_READ)==0;syncUs_+=micros(syncBegin,Clock::now());
    if(good){
     auto src=static_cast<const uint8_t*>(lease->mapping);
     auto pixelsBegin=Clock::now();
     auto yEnd=pixelsBegin;
     if(ten){
       size_t rows[3],cols[3];uint8_t* planes[3];
       for(unsigned plane=0;plane<3;++plane){rows[plane]=layout.planes[plane].rowInc;cols[plane]=layout.planes[plane].colInc;planes[plane]=p[plane];}
       sq2_copy_p010(src,g.stride,uv,g.width,g.height,planes,rows,cols);
     }else{
     for(unsigned y=0;y<g.height;y++){
      if(layout.planes[0].colInc==1)sq2_copy_row(p[0]+size_t(y)*layout.planes[0].rowInc,src+size_t(y)*g.stride,g.width);
      else for(unsigned x=0;x<g.width;x++)p[0][size_t(y)*layout.planes[0].rowInc+x*layout.planes[0].colInc]=src[size_t(y)*g.stride+x];
     }
     yEnd=Clock::now();yUs_+=micros(pixelsBegin,yEnd);
     if(layout.planes[1].colInc==2&&layout.planes[2].colInc==2&&layout.planes[1].rowInc==layout.planes[2].rowInc&&uintptr_t(p[2])==uintptr_t(p[1])+1){
      for(unsigned y=0;y<g.height/2;y++)sq2_copy_row(p[1]+size_t(y)*layout.planes[1].rowInc,src+uv+size_t(y)*g.stride,g.width);
     }else if(layout.planes[1].colInc==2&&layout.planes[2].colInc==2&&layout.planes[1].rowInc==layout.planes[2].rowInc&&uintptr_t(p[1])==uintptr_t(p[2])+1){
      for(unsigned y=0;y<g.height/2;y++)sq2_swap_uv(src+uv+size_t(y)*g.stride,p[2]+size_t(y)*layout.planes[2].rowInc,g.width/2);
     }else if(layout.planes[1].colInc==1&&layout.planes[2].colInc==1){
      for(unsigned y=0;y<g.height/2;y++)sq2_split_uv(src+uv+size_t(y)*g.stride,p[1]+size_t(y)*layout.planes[1].rowInc,p[2]+size_t(y)*layout.planes[2].rowInc,g.width/2);
     }else for(unsigned plane=1;plane<3;plane++)for(unsigned y=0;y<g.height/2;y++)for(unsigned x=0;x<g.width/2;x++)
      p[plane][size_t(y)*layout.planes[plane].rowInc+x*layout.planes[plane].colInc]=src[uv+size_t(y)*g.stride+2*x+plane-1];
     }
     syncBegin=Clock::now();uvUs_+=micros(yEnd,syncBegin);good=lease->sync(DMA_BUF_SYNC_END|DMA_BUF_SYNC_READ)==0;syncUs_+=micros(syncBegin,Clock::now());
    }
   }
   copied=Clock::now();
  }
  auto unmapped=Clock::now();
  if(!good){complete(frame.token,{},C2_CORRUPTED);return;}
  ++timedFrames_;fetchUs_+=micros(begin,fetched);mapUs_+=micros(fetched,mapped);copyUs_+=micros(mapped,copied);unmapUs_+=micros(copied,unmapped);
  complete(frame.token,C2Buffer::CreateGraphicBuffer(output->share(C2Rect(g.width,g.height),C2Fence())),C2_OK);
 }
 void failPending(){auto all=pending_;for(auto token:all)complete(token,{},C2_CORRUPTED);}
public:
 explicit Sq2C2V4L2Component(const std::shared_ptr<C2ComponentInterface>& i,Sq2Codec codec=Sq2Codec::Avc):SimpleC2Component(i),codec_(sq2CodecInfo(codec).fourcc){}
 c2_status_t onInit()override{return C2_OK;}
 c2_status_t onStop()override{
  fprintf(stderr,"SQ2_C2_STOP_BEGIN state=%d pending=%zu\n",int(session_.state()),pending_.size());
  __android_log_print(ANDROID_LOG_INFO,"SQ2C2Lifecycle","STOP_BEGIN state=%d pending=%zu",int(session_.state()),pending_.size());
  if(timedFrames_)__android_log_print(ANDROID_LOG_INFO,"SQ2C2Lifecycle","OUTPUT_TIMING frames=%llu fetch_us=%llu map_us=%llu copy_sync_us=%llu unmap_us=%llu",(unsigned long long)timedFrames_,(unsigned long long)fetchUs_,(unsigned long long)mapUs_,(unsigned long long)copyUs_,(unsigned long long)unmapUs_);
  if(timedFrames_)__android_log_print(ANDROID_LOG_INFO,"SQ2C2Lifecycle","DMA_SYNC us=%llu",(unsigned long long)syncUs_);
  if(timedFrames_)__android_log_print(ANDROID_LOG_INFO,"SQ2C2Lifecycle","PIXELS y_us=%llu uv_us=%llu",(unsigned long long)yUs_,(unsigned long long)uvUs_);
  if(timedFrames_)__android_log_print(ANDROID_LOG_INFO,"SQ2C2Lifecycle","OUTPUT_UNIQUE count=%zu",outputInodes_.size());outputInodes_.clear();
  timedFrames_=fetchUs_=mapUs_=copyUs_=unmapUs_=syncUs_=yUs_=uvUs_=0;
  int r=session_.close();fprintf(stderr,"SQ2_C2_SESSION_CLOSED result=%d\n",r);
  __android_log_print(ANDROID_LOG_INFO,"SQ2C2Lifecycle","SESSION_CLOSED result=%d",r);
  pending_.clear();pool_.reset();config_.clear();configured_.clear();collectingConfig_=false;
  if(!imports_.empty())__android_log_print(ANDROID_LOG_INFO,"SQ2C2Lifecycle","IMPORT_RELEASE count=%zu bytes=%zu",imports_.size(),importBytes_);
  imports_.clear();importBytes_=0;
  fprintf(stderr,"SQ2_C2_STOP_END result=%d\n",r);return r?C2_CORRUPTED:C2_OK;
 }
 void onReset()override{onStop();}
 void onRelease()override{onStop();}
 c2_status_t onFlush_sm()override{
  // MediaCodec flush keeps stream configuration. Reopened hardware needs CSD
  // again even when the extractor seeks to a keyframe without in-band headers.
  auto saved=configured_;auto result=onStop();configured_=saved;config_=saved;return result;
 }
 c2_status_t drain(uint32_t,const std::shared_ptr<C2BlockPool>&)override{return C2_OMITTED;}
 void process(const std::unique_ptr<C2Work>& w,const std::shared_ptr<C2BlockPool>& pool)override{
  if(w->worklets.size()!=1){w->result=C2_BAD_VALUE;w->workletsProcessed=w->worklets.size();return;}
  current_=&w;uint64_t token=w->input.ordinal.frameIndex.peeku();
  if(pending_.count(token)){w->result=C2_BAD_VALUE;w->workletsProcessed=1;current_=nullptr;return;}
  pending_.insert(token);w->workletsProcessed=0;
  if((w->input.flags&C2FrameData::FLAG_CODEC_CONFIG)!=0){
   // VPx frames are self-contained. WebM may supply container CSD metadata;
   // consume it like AOSP C2SoftVpxDec, never prefix it to a hardware frame.
   if(codec_==V4L2_PIX_FMT_VP8||codec_==V4L2_PIX_FMT_VP9){
    bool valid=!(w->input.flags&C2FrameData::FLAG_END_OF_STREAM)&&w->input.buffers.size()<=1;
    if(valid&&!w->input.buffers.empty()){
     valid=bool(w->input.buffers[0]);
     if(valid){auto blocks=w->input.buffers[0]->data().linearBlocks();valid=blocks.size()==1;
      if(valid){auto view=blocks[0].map().get();valid=view.error()==C2_OK&&view.capacity()<=Sq2AvcConfig::Limit;}
     }
    }
    complete(token,{},valid?C2_OK:C2_BAD_VALUE);current_=nullptr;return;
   }
   if(!collectingConfig_){config_.clear();configured_.clear();collectingConfig_=true;}
   int r=-EINVAL;
   if(!(w->input.flags&C2FrameData::FLAG_END_OF_STREAM)&&w->input.buffers.size()==1&&w->input.buffers[0]){
    auto blocks=w->input.buffers[0]->data().linearBlocks();
    if(blocks.size()==1){auto view=blocks[0].map().get();
     if(view.error()==C2_OK){r=config_.append(view.data(),view.capacity());if(!r)r=configured_.append(view.data(),view.capacity());}
    }
   }
   if(r){config_.clear();configured_.clear();}
   complete(token,{},r?C2_BAD_VALUE:C2_OK);current_=nullptr;return;
  }
  collectingConfig_=false;
  if(session_.state()==Sq2V4L2Session::State::Closed){
   C2StreamPictureSizeInfo::output size(0u);C2StreamPixelFormatInfo::output pixel(0u);std::vector<std::unique_ptr<C2Param>> params;
   if(intf()->query_vb({&size,&pixel},{},C2_MAY_BLOCK,&params)!=C2_OK){failPending();current_=nullptr;return;}
   if(pixel.value!=0x23&&pixel.value!=0x36){failPending();current_=nullptr;return;}
   outputFormat_=pixel.value==0x36?V4L2_PIX_FMT_P010:V4L2_PIX_FMT_NV12;
   const char* direct=getenv("SQ2_P010_DIRECT");
   directP010_=outputFormat_==V4L2_PIX_FMT_P010&&direct&&!strcmp(direct,"1");
   pool_=pool;
   int r=session_.open("/dev/video0",codec_,size.width,size.height,[this](const Sq2Geometry& g,Sq2CaptureBuffer& b){return allocate(g,b);},[this](Sq2DecodedFrame f){deliver(std::move(f));},!directP010_,outputFormat_);
   if(r){failPending();current_=nullptr;return;}
  }
  int r=0;
  if(!w->input.buffers.empty()){
   if(w->input.buffers.size()!=1||!w->input.buffers[0])r=-EINVAL;
   else{
    auto blocks=w->input.buffers[0]->data().linearBlocks();if(blocks.size()!=1)r=-EINVAL;
    else{auto view=blocks[0].map().get();if(view.error()!=C2_OK)r=-EIO;
     else{r=config_.submit(view.data(),view.capacity(),[&](const uint8_t* data,size_t size){
      int result=session_.submit(data,size,token);
      for(int tries=0;result==-EAGAIN&&tries<20;tries++){result=session_.pump(10);if(!result)result=session_.submit(data,size,token);}
      return result;
     });}
    }
   }
  }
  bool eos=(w->input.flags&C2FrameData::FLAG_END_OF_STREAM)!=0;
  if(!r&&eos){r=session_.requestDrain();auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
   while(!r&&session_.state()!=Sq2V4L2Session::State::Ended&&std::chrono::steady_clock::now()<deadline)r=session_.pump(20);
   if(!r&&session_.state()!=Sq2V4L2Session::State::Ended)r=-ETIMEDOUT;
   if(!r){
    if(w->workletsProcessed)w->worklets.front()->output.flags=C2FrameData::FLAG_END_OF_STREAM;
    else complete(token,{},C2_OK,true);
    if(!pending_.empty())failPending();
   }
  }else if(!r&&!w->input.buffers.empty())r=session_.pump(20);
  else if(!r)complete(token,{},C2_BAD_VALUE);
  // submit copied the entire access unit into a V4L2-owned input mapping.
  // Return the framework input slot even when its reordered output is pending.
  w->input.buffers.clear();
  if(r)failPending();current_=nullptr;
 }
};
