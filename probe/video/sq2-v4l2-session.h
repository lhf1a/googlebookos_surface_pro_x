#pragma once
// Stateful V4L2 session. No vector data, module loading, firmware or service control.
#include <linux/videodev2.h>
#include "sq2-video-memory.h"
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>
#ifndef SQ2_SESSION_TRACE
#define SQ2_SESSION_TRACE(stage) ((void)0)
#endif
#ifndef SQ2_SESSION_ERROR
#define SQ2_SESSION_ERROR(error) ((void)0)
#endif
struct Sq2Geometry { uint32_t width,height,stride,scanlines,bytes,count; uint32_t format=V4L2_PIX_FMT_NV12; };
struct Sq2CaptureBuffer { int fd=-1; size_t bytes=0; std::shared_ptr<void> owner; };
struct Sq2DecodedFrame { Sq2CaptureBuffer buffer; Sq2Geometry geometry; uint64_t token; uint32_t bytes,offset,flags; };
class Sq2V4L2Session {
public:
 using Allocate=std::function<int(const Sq2Geometry&,Sq2CaptureBuffer&)>;
 using Deliver=std::function<void(Sq2DecodedFrame)>;
 enum class State { Closed,Ready,Running,Draining,Ended,Failed };
private:
 struct Input {void* ptr=nullptr;size_t bytes=0;bool busy=false;};
 int fd_=-1,error_=0;bool outOn_=false,capOn_=false,source_=false,drainRequested_=false,reuseCapture_=false;
 State state_=State::Closed;std::vector<Input> input_;std::vector<Sq2CaptureBuffer> capture_;
 std::vector<bool> refill_;
 uint32_t outputFormat_=V4L2_PIX_FMT_NV12;
 Sq2Geometry geometry_{};Allocate allocate_;Deliver deliver_;
 int call(unsigned long op,void* arg){int r;do{r=ioctl(fd_,op,arg);}while(r<0&&errno==EINTR);return r<0?-errno:0;}
 int fail(int e){SQ2_SESSION_ERROR(e);error_=e;state_=State::Failed;return e;}
 int stream(unsigned type,bool on){return call(on?VIDIOC_STREAMON:VIDIOC_STREAMOFF,&type);}
 int queueCapture(unsigned i,bool allocate=true){
  auto& slot=capture_[i];if(allocate&&!slot.owner){Sq2CaptureBuffer next;int r=allocate_(geometry_,next);if(r)return r;slot=std::move(next);}
  if(slot.fd<0||!slot.owner||slot.bytes<geometry_.bytes||slot.bytes>sq2CaptureFrameLimit(outputFormat_))return -EINVAL;
  v4l2_plane p{};p.length=slot.bytes;p.m.fd=slot.fd;
  v4l2_buffer b{};b.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;b.memory=V4L2_MEMORY_DMABUF;b.index=i;b.length=1;b.m.planes=&p;
  int r=call(VIDIOC_QBUF,&b);if(!r)refill_[i]=false;return r;
 }
 int configureCapture(){
  if(source_)return -ENOTSUP; // Dynamic source changes require a later explicit reconfiguration path.
  v4l2_format f{};f.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;int r=call(VIDIOC_G_FMT,&f);if(r)return r;
  auto& p=f.fmt.pix_mp;
  if(p.pixelformat!=outputFormat_){p.pixelformat=outputFormat_;if((r=call(VIDIOC_S_FMT,&f)))return r;}
  v4l2_selection s{};s.type=V4L2_BUF_TYPE_VIDEO_CAPTURE;s.target=V4L2_SEL_TGT_COMPOSE;if((r=call(VIDIOC_G_SELECTION,&s)))return r;
  const unsigned sampleBytes=outputFormat_==V4L2_PIX_FMT_P010?2:1;
  const uint64_t rowBytes=uint64_t(s.r.width)*sampleBytes;
  if(p.num_planes!=1||p.pixelformat!=outputFormat_||s.r.left||s.r.top||!s.r.width||!s.r.height||
     (s.r.width&1)||(s.r.height&1)||s.r.width>8192||rowBytes>p.plane_fmt[0].bytesperline||
     s.r.height>p.height||p.height>8192||p.plane_fmt[0].bytesperline>16384||
     p.plane_fmt[0].bytesperline%sampleBytes||p.plane_fmt[0].sizeimage>sq2CaptureFrameLimit(outputFormat_))return -EINVAL;
  const uint64_t minimumBytes=uint64_t(p.plane_fmt[0].bytesperline)*(p.height+s.r.height/2-1)+rowBytes;
  if(minimumBytes>p.plane_fmt[0].sizeimage)return -EINVAL;
  v4l2_control ctrl{};ctrl.id=V4L2_CID_MIN_BUFFERS_FOR_CAPTURE;if((r=call(VIDIOC_G_CTRL,&ctrl)))return r;
  if(ctrl.value<1||ctrl.value>28)return -EINVAL;
  v4l2_requestbuffers req{};req.type=f.type;req.memory=V4L2_MEMORY_DMABUF;req.count=ctrl.value+4;if((r=call(VIDIOC_REQBUFS,&req)))return r;
  // Keep a bounded native pool; P010 doubles the per-sample storage.
  // 13 aligned 4K P010 frames require about 311 MiB.
  if(!req.count||req.count>32||uint64_t(req.count)*p.plane_fmt[0].sizeimage>sq2CapturePoolLimit(outputFormat_))return -EOVERFLOW;
  geometry_={s.r.width,s.r.height,p.plane_fmt[0].bytesperline,p.height,p.plane_fmt[0].sizeimage,req.count,outputFormat_};capture_.resize(req.count);refill_.assign(req.count,false);
  for(unsigned i=0;i<req.count;i++)if((r=queueCapture(i)))return r;
  if((r=stream(f.type,true)))return r;capOn_=true;source_=true;return 0;
 }
public:
 ~Sq2V4L2Session(){close();}
 Sq2V4L2Session()=default;Sq2V4L2Session(const Sq2V4L2Session&)=delete;Sq2V4L2Session& operator=(const Sq2V4L2Session&)=delete;
 State state()const{return state_;}int error()const{return error_;}
 // reuseCapture requires deliver to synchronously COPY the pixels before return;
 // no consumer may retain access to those pixels after this callback.
 int open(const char* device,uint32_t codec,uint32_t width,uint32_t height,Allocate allocate,Deliver deliver,bool reuseCapture=false,uint32_t outputFormat=V4L2_PIX_FMT_NV12){
  if(state_!=State::Closed)return -EBUSY;
  if(!device||!allocate||!deliver||!width||!height||width>8192||height>8192)return -EINVAL;
  if(outputFormat!=V4L2_PIX_FMT_NV12&&outputFormat!=V4L2_PIX_FMT_P010)return -EINVAL;
  if(outputFormat==V4L2_PIX_FMT_P010&&codec!=V4L2_PIX_FMT_HEVC&&codec!=V4L2_PIX_FMT_VP9)return -EINVAL;
  outputFormat_=outputFormat;
  reuseCapture_=reuseCapture;allocate_=std::move(allocate);deliver_=std::move(deliver);fd_=::open(device,O_RDWR|O_NONBLOCK|O_CLOEXEC);if(fd_<0)return -errno;
  v4l2_capability c{};int r=call(VIDIOC_QUERYCAP,&c);if(r)return fail(r);if(strcmp((char*)c.driver,"qcom-venus"))return fail(-ENODEV);
  for(unsigned type:{V4L2_EVENT_SOURCE_CHANGE,V4L2_EVENT_EOS}){v4l2_event_subscription s{};s.type=type;if((r=call(VIDIOC_SUBSCRIBE_EVENT,&s)))return fail(r);}
  v4l2_format f{};f.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;f.fmt.pix_mp.width=width;f.fmt.pix_mp.height=height;f.fmt.pix_mp.pixelformat=codec;f.fmt.pix_mp.field=V4L2_FIELD_NONE;if((r=call(VIDIOC_S_FMT,&f)))return fail(r);
  if(f.fmt.pix_mp.pixelformat!=codec||f.fmt.pix_mp.num_planes!=1)return fail(-EINVAL);
  v4l2_requestbuffers req{};req.type=f.type;req.memory=V4L2_MEMORY_MMAP;req.count=6;if((r=call(VIDIOC_REQBUFS,&req)))return fail(r);if(!req.count||req.count>32)return fail(-EINVAL);input_.resize(req.count);size_t total=0;
  for(unsigned i=0;i<req.count;i++){v4l2_plane p{};v4l2_buffer b{};b.type=f.type;b.memory=req.memory;b.index=i;b.length=1;b.m.planes=&p;if((r=call(VIDIOC_QUERYBUF,&b)))return fail(r);if(b.length!=1||!p.length||p.length>16*1024*1024||(total+=p.length)>128*1024*1024)return fail(-EOVERFLOW);void* ptr=mmap(nullptr,p.length,PROT_READ|PROT_WRITE,MAP_SHARED,fd_,p.m.mem_offset);if(ptr==MAP_FAILED)return fail(-errno);input_[i]={ptr,p.length,false};}
  state_=State::Ready;return 0;
 }
 // Token is an opaque C2 frame index, not a fixed vector index or presentation timestamp.
 int submit(const uint8_t* bytes,size_t length,uint64_t token){
  if(state_!=State::Ready&&state_!=State::Running)return -EPIPE;
  if(drainRequested_||!bytes||!length||token>uint64_t(INT64_MAX))return -EINVAL;
  unsigned i=0;while(i<input_.size()&&input_[i].busy)i++;if(i==input_.size())return -EAGAIN;
  auto& slot=input_[i];if(length>slot.bytes)return -EMSGSIZE;memcpy(slot.ptr,bytes,length);
  v4l2_plane p{};p.length=slot.bytes;p.bytesused=length;v4l2_buffer b{};b.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;b.memory=V4L2_MEMORY_MMAP;b.index=i;b.length=1;b.m.planes=&p;b.timestamp.tv_sec=token/1000000;b.timestamp.tv_usec=token%1000000;
  int r=call(VIDIOC_QBUF,&b);if(r)return fail(r);slot.busy=true;
  if(!outOn_){if((r=stream(b.type,true)))return fail(r);outOn_=true;}state_=State::Running;return 0;
 }
 int requestDrain(){if(state_!=State::Running)return -EPIPE;drainRequested_=true;return 0;}
 int pump(int timeoutMs){
  if(state_==State::Failed)return error_;if(state_!=State::Running&&state_!=State::Draining)return -EPIPE;
  if(timeoutMs<0||timeoutMs>1000)return -EINVAL;
  // A downstream client may still own the returned graphic block. Retry vacant
  // slots once per pump, without recycling that client's pixels or busy-waiting.
  for(unsigned i=0;i<refill_.size();++i)if(refill_[i]){
   int r=queueCapture(i,!reuseCapture_);if(r&&r!=-EAGAIN)return fail(r);
  }
  pollfd pfd{fd_,short(POLLIN|POLLPRI),0};int pr;do{pr=poll(&pfd,1,timeoutMs);}while(pr<0&&errno==EINTR);if(pr<0)return fail(-errno);
  for(unsigned n=0;n<32;n++){v4l2_event e{};int r=call(VIDIOC_DQEVENT,&e);if(r==-EAGAIN||r==-ENOENT)break;if(r)return fail(r);if(e.type==V4L2_EVENT_SOURCE_CHANGE&&(r=configureCapture()))return fail(r);}
  for(unsigned n=0;n<input_.size();n++){v4l2_plane p{};v4l2_buffer b{};b.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;b.memory=V4L2_MEMORY_MMAP;b.length=1;b.m.planes=&p;int r=call(VIDIOC_DQBUF,&b);if(r==-EAGAIN)break;if(r)return fail(r);if(b.index>=input_.size()||!input_[b.index].busy||(b.flags&V4L2_BUF_FLAG_ERROR))return fail(-EIO);input_[b.index].busy=false;}
  bool busy=false;for(auto& i:input_)busy|=i.busy;
  if(drainRequested_&&!busy&&state_==State::Running){v4l2_decoder_cmd cmd{};cmd.cmd=V4L2_DEC_CMD_STOP;int r=call(VIDIOC_DECODER_CMD,&cmd);if(r)return fail(r);state_=State::Draining;}
  if(capOn_)for(unsigned n=0;n<capture_.size();n++){
   v4l2_plane p{};v4l2_buffer b{};b.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;b.memory=V4L2_MEMORY_DMABUF;b.length=1;b.m.planes=&p;int r=call(VIDIOC_DQBUF,&b);if(r==-EAGAIN)break;if(r)return fail(r);
   if(b.index>=capture_.size()||refill_[b.index]||p.bytesused>capture_[b.index].bytes||p.data_offset>p.bytesused||(b.flags&V4L2_BUF_FLAG_ERROR)||b.timestamp.tv_sec<0||b.timestamp.tv_usec<0||b.timestamp.tv_usec>=1000000)return fail(-EIO);
   auto buffer=reuseCapture_?capture_[b.index]:std::move(capture_[b.index]);
   if(!reuseCapture_)capture_[b.index]={};
   refill_[b.index]=true;
   if(p.bytesused>p.data_offset){
    const uint64_t rowBytes=uint64_t(geometry_.width)*(geometry_.format==V4L2_PIX_FMT_P010?2:1);
    const uint64_t minimumBytes=uint64_t(geometry_.stride)*(geometry_.scanlines+geometry_.height/2-1)+rowBytes;
    if(minimumBytes>p.bytesused-p.data_offset)return fail(-EIO);
    deliver_({std::move(buffer),geometry_,uint64_t(b.timestamp.tv_sec)*1000000+uint64_t(b.timestamp.tv_usec),p.bytesused,p.data_offset,b.flags});
   }
   if(b.flags&V4L2_BUF_FLAG_LAST){refill_[b.index]=false;state_=State::Ended;break;}
   r=queueCapture(b.index,!reuseCapture_);if(r&&r!=-EAGAIN)return fail(r);
  }
  return 0;
 }
 int close(){
  int result=0;if(fd_>=0){SQ2_SESSION_TRACE("capture-streamoff");if(capOn_){int r=stream(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,false);if(r)result=r;}SQ2_SESSION_TRACE("output-streamoff");if(outOn_){int r=stream(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,false);if(r&&!result)result=r;}
   SQ2_SESSION_TRACE("input-unmap");
   for(auto& i:input_)if(i.ptr&&munmap(i.ptr,i.bytes)&&!result)result=-errno;
   SQ2_SESSION_TRACE("fd-close");if(::close(fd_)&&!result)result=-errno;SQ2_SESSION_TRACE("fd-closed");
  }
  fd_=-1;input_.clear();SQ2_SESSION_TRACE("capture-release");capture_.clear();refill_.clear();SQ2_SESSION_TRACE("capture-released");allocate_={};deliver_={};outOn_=capOn_=source_=drainRequested_=false;state_=State::Closed;error_=0;return result;
 }
};
