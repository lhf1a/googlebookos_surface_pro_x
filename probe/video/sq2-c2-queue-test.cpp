// Component lifecycle/queue ABI test only. No decoder or registered service.
#include "sq2-c2-interface.h"
#include <SimpleC2Component.h>
#include <condition_variable>
#include <chrono>
#include <mutex>
class QueueProbe final:public android::SimpleC2Component{
public:
 explicit QueueProbe(const std::shared_ptr<C2ComponentInterface>& i):SimpleC2Component(i){}
 c2_status_t onInit()override{return C2_OK;}
 c2_status_t onStop()override{return C2_OK;}
 void onReset()override{}
 void onRelease()override{}
 c2_status_t onFlush_sm()override{return C2_OK;}
 void process(const std::unique_ptr<C2Work>& w,const std::shared_ptr<C2BlockPool>&)override{
  // Never report undecoded input as a successful video frame.
  w->result=C2_OMITTED;w->workletsProcessed=w->worklets.size();
 }
 c2_status_t drain(uint32_t,const std::shared_ptr<C2BlockPool>&)override{return C2_OMITTED;}
};
class Listener final:public C2Component::Listener{
public:
 std::mutex mutex;std::condition_variable cv;unsigned done=0,errors=0;bool valid=true;
 void onWorkDone_nb(std::weak_ptr<C2Component>,std::list<std::unique_ptr<C2Work>> ws)override{
  std::lock_guard<std::mutex> lock(mutex);
  for(auto& w:ws){valid&=w->result==C2_OMITTED;done++;printf("WORK_DONE result=%d processed=%u\n",w->result,w->workletsProcessed);}cv.notify_all();
 }
 void onTripped_nb(std::weak_ptr<C2Component>,std::vector<std::shared_ptr<C2SettingResult>>)override{std::lock_guard<std::mutex> lock(mutex);errors++;cv.notify_all();}
 void onError_nb(std::weak_ptr<C2Component>,uint32_t e)override{std::lock_guard<std::mutex> lock(mutex);errors++;printf("ERROR=%u\n",e);cv.notify_all();}
};
int main(){
 setvbuf(stdout,nullptr,_IONBF,0);
 auto reflector=std::make_shared<C2ReflectorHelper>();auto impl=std::make_shared<Sq2DecodeInterface>(reflector,320,640,240,480);
 auto intf=std::make_shared<android::SimpleC2Interface<Sq2DecodeInterface>>("c2.sq2.queue.probe",0,impl);
 auto c=std::make_shared<QueueProbe>(intf);auto l=std::make_shared<Listener>();
 assert(c->setListener_vb(l,C2_MAY_BLOCK)==C2_OK);
 assert(c->start()==C2_OK);puts("START_OK");
 std::list<std::unique_ptr<C2Work>> ws;auto w=std::make_unique<C2Work>();w->input.flags=(C2FrameData::flags_t)0;w->input.ordinal.frameIndex=1;w->input.ordinal.timestamp=33333;w->worklets.emplace_back(new C2Worklet);ws.push_back(std::move(w));
 auto ret=c->queue_nb(&ws);printf("QUEUE ret=%d remaining=%zu\n",ret,ws.size());assert(ret==C2_OK&&ws.empty());
 {std::unique_lock<std::mutex> lock(l->mutex);assert(l->cv.wait_for(lock,std::chrono::seconds(5),[&]{return l->done||l->errors;}));assert(l->done==1&&!l->errors&&l->valid);}
 assert(c->stop()==C2_OK);puts("STOP_OK");assert(c->reset()==C2_OK);puts("RESET_OK");assert(c->release()==C2_OK);puts("RELEASE_OK");c.reset();puts("QUEUE_TEST_PASS");
}
