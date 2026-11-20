#include <V2_1/SubHal.h>
#include <atomic>
#include <cassert>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
namespace android::hardware::sensors::V2_0::implementation{
class ScopedWakelockTest{public:static ScopedWakelock make(){return ScopedWakelock(nullptr,false);}};
}
using android::hardware::Return;using android::hardware::Void;using android::hardware::hidl_vec;
namespace v1=android::hardware::sensors::V1_0;namespace v2=android::hardware::sensors::V2_1;
namespace impl=android::hardware::sensors::V2_1::implementation;
class Callback final:public impl::IHalProxyCallback{
 public:std::atomic<int> count{0},flushes{0};
 Return<void> onDynamicSensorsConnected(const hidl_vec<v1::SensorInfo>&)override{return Void();}
 Return<void> onDynamicSensorsConnected_2_1(const hidl_vec<v2::SensorInfo>&)override{return Void();}
 Return<void> onDynamicSensorsDisconnected(const hidl_vec<int32_t>&)override{return Void();}
 void postEvents(const std::vector<v2::Event>&events,ScopedWakelock w)override{
  assert(!w.isLocked());for(auto&e:events){
   if(e.sensorType==v2::SensorType::META_DATA){flushes++;continue;}
   count++;printf("event type=%d handle=%d time=%lld value=%g\n",int(e.sensorType),e.sensorHandle,(long long)e.timestamp,e.u.scalar);
   if(e.sensorType==v2::SensorType::ACCELEROMETER||e.sensorType==v2::SensorType::GYROSCOPE||e.sensorType==v2::SensorType::MAGNETIC_FIELD)
    printf("xyz %g %g %g accuracy=%d\n",e.u.vec3.x,e.u.vec3.y,e.u.vec3.z,int(e.u.vec3.status));
   fflush(stdout);
  }
 }
 ScopedWakelock createScopedWakelock(bool locked)override{assert(!locked);return android::hardware::sensors::V2_0::implementation::ScopedWakelockTest::make();}
};
int main(int argc,char**argv){
 assert(argc==2||argc==3);int handle=argc==3?std::atoi(argv[2]):2;assert(handle>=1&&handle<=4);void*lib=dlopen(argv[1],RTLD_NOW);if(!lib){puts(dlerror());return 1;}
 auto get=(impl::ISensorsSubHal*(*)(uint32_t*))dlsym(lib,"sensorsHalGetSubHal_2_1");assert(get);
 uint32_t version=0;auto*hal=get(&version);assert(version==SUB_HAL_2_1_VERSION);
 bool available=false;hal->getSensorsList_2_1([&](const auto&list){for(size_t i=0;i<list.size();i++){const auto&s=list[i];printf("sensor %d %s\n",s.sensorHandle,s.name.c_str());available|=s.sensorHandle==handle;}});
 assert(available);android::sp<Callback> cb=new Callback;
 assert(hal->initialize(cb)==v1::Result::OK);
 assert(hal->flush(handle)==v1::Result::BAD_VALUE);
 assert(hal->batch(handle,40000000,0)==v1::Result::OK);
 assert(hal->activate(handle,true)==v1::Result::OK);sleep(5);
 assert(hal->flush(handle)==v1::Result::OK);sleep(2);assert(cb->count>0&&cb->flushes==1);
 assert(hal->activate(handle,false)==v1::Result::OK);int count=cb->count;sleep(1);assert(cb->count==count);
 assert(hal->initialize(cb)==v1::Result::OK);
 const char* pass[]={"SUBHAL_ACCEL_PASS","SUBHAL_LIGHT_PASS","SUBHAL_GYRO_PASS","SUBHAL_MAG_PASS"};
 puts(pass[handle-1]);
}
