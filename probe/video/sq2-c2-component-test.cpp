// Independent component harness; pinned vector is test input, never compiled into the backend.
#include "sq2-c2-interface.h"
#include "sq2-c2-v4l2-component.h"
#include <C2PlatformSupport.h>
#include <condition_variable>
#include <mutex>
#include <cstdio>
typedef uint32_t u32;typedef uint8_t u8;
#ifdef SQ2_VPX_VECTOR
#include "sq2-vpx-vector.h"
static constexpr Sq2Codec testCodec=SQ2_VEC_CODEC==V4L2_PIX_FMT_VP8?Sq2Codec::Vp8:Sq2Codec::Vp9;
#else
#include "sq2-h264-vector.h"
static constexpr Sq2Codec testCodec=Sq2Codec::Avc;
#endif
static uint32_t crc(const uint8_t* p,unsigned stride,unsigned w,unsigned h){uint32_t c=~0u;for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){c^=p[y*stride+x];for(int b=0;b<8;b++)c=(c>>1)^(0xedb88320u&(0u-(c&1)));}return ~c;}
class TestListener final:public C2Component::Listener{
public:
 std::mutex m;std::condition_variable cv;unsigned done=0,mask=0,eos=0,errors=0;
 std::vector<std::shared_ptr<C2Buffer>> retained;
 void onWorkDone_nb(std::weak_ptr<C2Component>,std::list<std::unique_ptr<C2Work>> works)override{
  std::lock_guard<std::mutex> lock(m);
  for(auto& w:works){done++;auto idx=w->input.ordinal.frameIndex.peeku();printf("CALLBACK index=%llu result=%d processed=%u\n",(unsigned long long)idx,w->result,w->workletsProcessed);
   if(w->result!=C2_OK||w->workletsProcessed!=1){errors++;continue;}
   auto& o=w->worklets.front()->output;if(o.ordinal.frameIndex.peeku()!=idx||o.ordinal.timestamp.peekll()!=w->input.ordinal.timestamp.peekll())errors++;
   if(o.flags&C2FrameData::FLAG_END_OF_STREAM)eos++;
   for(auto& b:o.buffers){auto blocks=b->data().graphicBlocks();if(blocks.size()!=1){errors++;continue;}auto view=blocks[0].map().get();if(view.error()!=C2_OK){errors++;continue;}auto p=view.data();auto l=view.layout();
    if(idx<1||idx>6||l.numPlanes!=3){errors++;continue;}
    // Compare semantic Y/U/V samples, independent of I420/NV12/NV21 layout.
    std::vector<uint8_t> canonical(320*240*3/2);
    bool valid=true;
    for(unsigned plane=0;plane<3;plane++){
     const auto& lp=l.planes[plane];unsigned width=plane?160:320,height=plane?120:240;
     if(!p[plane]||lp.bitDepth!=8||lp.colInc<=0||lp.rowInc<=0){valid=false;break;}
     for(unsigned yy=0;yy<height;yy++)for(unsigned xx=0;xx<width;xx++)
      canonical[plane?320*240+yy*320+xx*2+plane-1:yy*320+xx]=p[plane][yy*lp.rowInc+xx*lp.colInc];
    }
    if(!valid){errors++;continue;}
    auto y=crc(canonical.data(),320,320,240),uv=crc(canonical.data()+320*240,320,320,120);
    printf("C2_FRAME index=%llu crc_y=%08x crc_uv=%08x\n",(unsigned long long)idx,y,uv);
    if(y!=sq2_vec_crc_y[idx-1]||uv!=sq2_vec_crc_uv[idx-1]||(mask&(1u<<(idx-1))))errors++;else mask|=1u<<(idx-1);retained.push_back(b);
   }
  }fsync(1);cv.notify_all();
 }
 void onTripped_nb(std::weak_ptr<C2Component>,std::vector<std::shared_ptr<C2SettingResult>>)override{std::lock_guard<std::mutex> lock(m);errors++;cv.notify_all();}
 void onError_nb(std::weak_ptr<C2Component>,uint32_t e)override{std::lock_guard<std::mutex> lock(m);errors++;printf("COMPONENT_ERROR=%u\n",e);cv.notify_all();}
};
int main(int argc,char** argv){
 bool configOnly=argc==2&&!strcmp(argv[1],"--config-only");
 setvbuf(stdout,nullptr,_IONBF,0);auto r=std::make_shared<C2ReflectorHelper>();auto p=std::make_shared<Sq2DecodeInterface>(r,320,640,240,480,testCodec);auto i=std::make_shared<android::SimpleC2Interface<Sq2DecodeInterface>>(sq2CodecInfo(testCodec).name,0,p);auto c=std::make_shared<Sq2C2V4L2Component>(i,testCodec);
 if(argc==1){auto e=c->release();printf("LIFECYCLE_RELEASE=%d NO_DEVICE_OPEN\n",e);return e!=C2_OK;}
 auto listener=std::make_shared<TestListener>();if(c->setListener_vb(listener,C2_MAY_BLOCK)||c->start())return 2;
 std::shared_ptr<C2BlockPool> pool;if(android::GetCodec2BlockPool(C2BlockPool::BASIC_LINEAR,c,&pool))return 3;
 std::list<std::unique_ptr<C2Work>> works;
 const unsigned count=configOnly?2:7;
 for(unsigned n=0;n<count;n++){
  auto w=std::make_unique<C2Work>();w->input.ordinal.frameIndex=n+1;w->input.ordinal.timestamp=(n+1)*33333;w->input.flags=n==6?C2FrameData::FLAG_END_OF_STREAM:C2FrameData::flags_t(0);w->worklets.emplace_back(new C2Worklet);
  if(configOnly)w->input.flags=C2FrameData::FLAG_CODEC_CONFIG;
  // Config-only uses opaque bytes: this checks C2 ownership/completion, not H264 parsing.
  if(n<6){unsigned size=configOnly?16:sq2_vec_au[n+1]-sq2_vec_au[n];std::shared_ptr<C2LinearBlock> block;if(pool->fetchLinearBlock(size,C2MemoryUsage(C2MemoryUsage::CPU_READ|C2MemoryUsage::CPU_WRITE),&block))return 4;
   {auto view=block->map().get();if(view.error()!=C2_OK)return 5;memcpy(view.data(),sq2_vec_stream+sq2_vec_au[n],size);}
   w->input.buffers.push_back(C2Buffer::CreateLinearBuffer(block->share(0,size,C2Fence())));
  }works.push_back(std::move(w));
 }
 puts("C2_QUEUE_BEGIN");fsync(1);auto queued=c->queue_nb(&works);printf("C2_QUEUE_RETURN=%d\n",queued);fsync(1);
 bool good=false;{std::unique_lock<std::mutex> lock(listener->m);listener->cv.wait_for(lock,std::chrono::seconds(15),[&]{return listener->done==count||listener->errors;});good=listener->done==count&&listener->mask==(configOnly?0u:63u)&&listener->eos==(configOnly?0u:1u)&&!listener->errors;printf("C2_RESULT config_only=%d done=%u mask=%x eos=%u errors=%u\n",configOnly,listener->done,listener->mask,listener->eos,listener->errors);}
 auto stopped=c->stop();printf("C2_STOP=%d\n",stopped);auto released=c->release();printf("C2_RELEASE=%d\n",released);c.reset();listener->retained.clear();puts("CLIENT_BUFFERS_RELEASED");return !good||queued||stopped||released;
}
