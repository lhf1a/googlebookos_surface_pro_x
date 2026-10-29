#include "sq2-v4l2-session.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <map>
static unsigned submits,returns,frames,allocs,releases,closes,captureQueued;
static bool ten;static unsigned invalid;
static bool eventSent,drained,frameSent,lastSent,failReplacement;
static uint64_t token;static unsigned seenLength;static unsigned char seen[32];
static std::map<unsigned,void*> mappings;
extern "C" int __wrap_open(const char*,int,...){return 10;}
extern "C" int __wrap_close(int fd){assert(fd==10);closes++;return 0;}
extern "C" void* __wrap_mmap(void*,size_t n,int,int,int,off_t offset){void* p=calloc(1,n);mappings[offset]=p;return p;}
extern "C" int __wrap_munmap(void* p,size_t){for(auto it=mappings.begin();it!=mappings.end();it++)if(it->second==p){mappings.erase(it);break;}free(p);return 0;}
extern "C" int __wrap_poll(pollfd*,nfds_t,int){return 1;}
extern "C" int __wrap_ioctl(int,unsigned long op,...){va_list ap;va_start(ap,op);void* arg=va_arg(ap,void*);va_end(ap);
 switch(op){
 case VIDIOC_QUERYCAP:std::strcpy((char*)((v4l2_capability*)arg)->driver,"qcom-venus");return 0;
 case VIDIOC_SUBSCRIBE_EVENT:case VIDIOC_STREAMON:case VIDIOC_STREAMOFF:return 0;
 case VIDIOC_S_FMT:{auto f=(v4l2_format*)arg;f->fmt.pix_mp.num_planes=1;if(invalid==2&&f->type==V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)f->fmt.pix_mp.pixelformat=V4L2_PIX_FMT_NV12;return 0;}
 case VIDIOC_REQBUFS:return 0;
 case VIDIOC_QUERYBUF:{auto b=(v4l2_buffer*)arg;b->m.planes[0].length=4096;b->m.planes[0].m.mem_offset=b->index*4096;return 0;}
 case VIDIOC_QBUF:{auto b=(v4l2_buffer*)arg;if(b->type==V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE){submits++;token=uint64_t(b->timestamp.tv_sec)*1000000+b->timestamp.tv_usec;seenLength=b->m.planes[0].bytesused;assert(seenLength<=sizeof(seen));memcpy(seen,mappings[b->index*4096],seenLength);}else captureQueued++;return 0;}
 case VIDIOC_DQEVENT:if(!eventSent){eventSent=true;((v4l2_event*)arg)->type=V4L2_EVENT_SOURCE_CHANGE;return 0;}errno=ENOENT;return -1;
 case VIDIOC_G_FMT:{auto f=(v4l2_format*)arg;f->fmt.pix_mp.pixelformat=ten&&invalid!=2?V4L2_PIX_FMT_P010:V4L2_PIX_FMT_NV12;f->fmt.pix_mp.num_planes=1;f->fmt.pix_mp.height=4;f->fmt.pix_mp.plane_fmt[0].bytesperline=ten&&invalid!=1?8:4;f->fmt.pix_mp.plane_fmt[0].sizeimage=invalid==3?47:4096;return 0;}
 case VIDIOC_G_SELECTION:{auto s=(v4l2_selection*)arg;s->r.width=s->r.height=4;return 0;}
 case VIDIOC_G_CTRL:((v4l2_control*)arg)->value=1;return 0;
 case VIDIOC_DQBUF:{auto b=(v4l2_buffer*)arg;if(b->type==V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE){if(returns<submits){returns++;b->index=0;return 0;}}
 else if(!frameSent){frameSent=true;b->index=0;b->m.planes[0].bytesused=invalid==4?47:ten?48:24;b->timestamp.tv_sec=token/1000000;b->timestamp.tv_usec=token%1000000;return 0;}
 else if(drained&&!lastSent){lastSent=true;b->index=1;b->flags=V4L2_BUF_FLAG_LAST;return 0;}errno=EAGAIN;return -1;}
 case VIDIOC_DECODER_CMD:drained=true;return 0;
 }fprintf(stderr,"unexpected ioctl=%lu\n",op);abort();}
int main(){
 for(bool depth:{false,true})for(unsigned mode:{0u,1u,2u}){
 ten=depth;invalid=0;
 bool inject=mode==1,reuse=mode==2;
 submits=returns=frames=allocs=releases=closes=captureQueued=0;eventSent=drained=frameSent=lastSent=false;failReplacement=inject;
 std::shared_ptr<void> held;
 Sq2V4L2Session s;auto allocate=[&](const Sq2Geometry& g,Sq2CaptureBuffer& b){assert(g.width==4&&g.count==5&&g.format==(ten?V4L2_PIX_FMT_P010:V4L2_PIX_FMT_NV12));if(failReplacement&&allocs==5)return -ENOMEM;allocs++;b={30,4096,std::shared_ptr<void>(new int(0),[](void* p){releases++;delete (int*)p;})};return 0;};
 auto deliver=[&](Sq2DecodedFrame f){assert(f.token==123456789&&f.bytes==(ten?48u:24u)&&f.offset==0);frames++;if(!reuse)held=f.buffer.owner;};
 assert(s.open("/dev/mock",ten?V4L2_PIX_FMT_HEVC:V4L2_PIX_FMT_H264,4,4,allocate,deliver,reuse,ten?V4L2_PIX_FMT_P010:V4L2_PIX_FMT_NV12)==0);
 const uint8_t payload[]={9,7,5,3,1};assert(s.submit(payload,sizeof(payload),123456789)==0);assert(seenLength==sizeof(payload)&&!memcmp(payload,seen,sizeof(payload)));
 int ret=s.pump(0);assert(frames==1&&(reuse||held)&&releases==0);
 if(inject){assert(ret==-ENOMEM&&s.state()==Sq2V4L2Session::State::Failed);assert(s.pump(0)==-ENOMEM);puts("REPLACEMENT_FAILURE_PRESERVES_CLIENT_LEASE");}
 else{
  assert(ret==0&&allocs==(reuse?5u:6u)&&captureQueued==6);
  if(reuse)for(unsigned n=1;n<100;n++){frameSent=false;assert(s.submit(payload,sizeof(payload),123456789)==0);assert(s.pump(0)==0);assert(allocs==5&&releases==0);}
  if(reuse){assert(frames==100&&captureQueued==105);puts("FIXED_CAPTURE_100_FRAMES_5_ALLOCATIONS");}
  assert(s.requestDrain()==0);assert(s.pump(0)==0&&s.state()==Sq2V4L2Session::State::Ended);puts("PAYLOAD_TOKEN_REPLACEMENT_EOS_PASS");
 }
 assert(s.close()==0&&closes==1&&mappings.empty());assert(releases==allocs-(reuse?0:1));held.reset();assert(releases==allocs);puts("CLOSE_AND_CLIENT_RELEASE_PASS");
 }
 for(unsigned fault=1;fault<=4;fault++){
  ten=true;invalid=fault;eventSent=drained=frameSent=lastSent=false;
  submits=returns=frames=allocs=releases=closes=captureQueued=0;
  Sq2V4L2Session s;
  auto allocate=[](const Sq2Geometry&,Sq2CaptureBuffer& b){allocs++;b={30,4096,std::shared_ptr<void>(new int,[](void* p){releases++;delete (int*)p;})};return 0;};
  auto deliver=[](Sq2DecodedFrame){frames++;};
  assert(s.open("/dev/mock",V4L2_PIX_FMT_H264,4,4,allocate,deliver,false,V4L2_PIX_FMT_P010)==-EINVAL);
  assert(s.open("/dev/mock",V4L2_PIX_FMT_HEVC,4,4,allocate,deliver,false,V4L2_PIX_FMT_P010)==0);
  const uint8_t bytes[]={1,2};assert(!s.submit(bytes,2,1));
  assert(s.pump(0)==(fault==4?-EIO:-EINVAL));assert(frames==0);
  assert(!s.close()&&closes==1&&mappings.empty()&&allocs==releases);
 }
 puts("P010_STRIDE_SUBSTITUTION_SHORT_ALLOCATION_SHORT_PAYLOAD_REFUSED");

}
