#pragma once
// Experimental in-process adapter. Not registered and not a complete MediaCodec implementation.
#include "sq2-v4l2-session.h"
#include <SimpleC2Component.h>
#include <C2AllocatorGralloc.h>
#include <chrono>
#include <set>
#include <sys/stat.h>
class Sq2C2V4L2Component final:public android::SimpleC2Component {
 struct Lease {std::shared_ptr<C2GraphicBlock> block;int fd=-1;~Lease(){if(fd>=0)::close(fd);}};
 Sq2V4L2Session session_;std::shared_ptr<C2BlockPool> pool_;
 const std::unique_ptr<C2Work>* current_=nullptr;std::set<uint64_t> pending_;
 int allocate(const Sq2Geometry& g,Sq2CaptureBuffer& out){
  auto lease=std::make_shared<Lease>();
  C2MemoryUsage usage{C2MemoryUsage::CPU_READ|C2MemoryUsage::CPU_WRITE};
  if(!pool_||pool_->fetchGraphicBlock(g.width,g.height,0x23,usage,&lease->block)!=C2_OK)return -ENOMEM;
  {
   auto view=lease->block->map().get();if(view.error()!=C2_OK)return -EIO;
   const auto& layout=view.layout();auto p=view.data();
   if(layout.type!=C2PlanarLayout::TYPE_YUV||layout.numPlanes!=3)return -ENOTSUP;
   for(unsigned i=0;i<3;i++)if(!p[i]||layout.planes[i].rowInc!=int(g.stride)||layout.planes[i].colInc!=(i?2:1)||layout.planes[i].bitDepth!=8)return -ENOTSUP;
   if(uintptr_t(p[1])-uintptr_t(p[0])!=uint64_t(g.stride)*g.scanlines||uintptr_t(p[2])-uintptr_t(p[1])!=1)return -ENOTSUP;
  }
  auto h=android::UnwrapNativeCodec2GrallocHandle(lease->block->handle());if(!h)return -EINVAL;
  struct stat a{},b{};bool valid=h->numFds>=2&&!fstat(h->data[0],&a)&&!fstat(h->data[1],&b)&&a.st_dev==b.st_dev&&a.st_ino==b.st_ino;
  off_t bytes=valid?lseek(h->data[0],0,SEEK_END):-1;
  if(bytes>=off_t(g.bytes)&&bytes<=16*1024*1024){lseek(h->data[0],0,SEEK_SET);lease->fd=fcntl(h->data[0],F_DUPFD_CLOEXEC,0);}free(h);
  if(lease->fd<0)return -EINVAL;out={lease->fd,size_t(bytes),lease};return 0;
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
  if(frame.offset){complete(frame.token,{},C2_CORRUPTED);return;}
  auto block=lease->block->share(C2Rect(frame.geometry.width,frame.geometry.height),C2Fence());
  complete(frame.token,C2Buffer::CreateGraphicBuffer(block),C2_OK);
 }
 void failPending(){auto all=pending_;for(auto token:all)complete(token,{},C2_CORRUPTED);}
public:
 explicit Sq2C2V4L2Component(const std::shared_ptr<C2ComponentInterface>& i):SimpleC2Component(i){}
 c2_status_t onInit()override{return C2_OK;}
 c2_status_t onStop()override{int r=session_.close();pending_.clear();pool_.reset();return r?C2_CORRUPTED:C2_OK;}
 void onReset()override{onStop();}
 void onRelease()override{onStop();}
 c2_status_t onFlush_sm()override{return onStop();}
 c2_status_t drain(uint32_t,const std::shared_ptr<C2BlockPool>&)override{return C2_OMITTED;}
 void process(const std::unique_ptr<C2Work>& w,const std::shared_ptr<C2BlockPool>& pool)override{
  if(w->worklets.size()!=1){w->result=C2_BAD_VALUE;w->workletsProcessed=w->worklets.size();return;}
  current_=&w;uint64_t token=w->input.ordinal.frameIndex.peeku();
  if(pending_.count(token)){w->result=C2_BAD_VALUE;w->workletsProcessed=1;current_=nullptr;return;}
  pending_.insert(token);w->workletsProcessed=0;
  if(session_.state()==Sq2V4L2Session::State::Closed){
   C2StreamPictureSizeInfo::output size(0u);std::vector<std::unique_ptr<C2Param>> params;
   if(intf()->query_vb({&size},{},C2_MAY_BLOCK,&params)!=C2_OK){failPending();current_=nullptr;return;}
   pool_=pool;
   int r=session_.open("/dev/video0",V4L2_PIX_FMT_H264,size.width,size.height,[this](const Sq2Geometry& g,Sq2CaptureBuffer& b){return allocate(g,b);},[this](Sq2DecodedFrame f){deliver(std::move(f));});
   if(r){failPending();current_=nullptr;return;}
  }
  int r=0;
  if(!w->input.buffers.empty()){
   if(w->input.buffers.size()!=1||!w->input.buffers[0])r=-EINVAL;
   else{
    auto blocks=w->input.buffers[0]->data().linearBlocks();if(blocks.size()!=1)r=-EINVAL;
    else{auto view=blocks[0].map().get();if(view.error()!=C2_OK)r=-EIO;
     else{r=session_.submit(view.data(),view.capacity(),token);for(int tries=0;r==-EAGAIN&&tries<20;tries++){r=session_.pump(10);if(!r)r=session_.submit(view.data(),view.capacity(),token);}}
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
  if(r)failPending();current_=nullptr;
 }
};
