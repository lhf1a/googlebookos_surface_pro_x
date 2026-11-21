// SPDX-License-Identifier: GPL-2.0-only
#include <V2_1/SubHal.h>
#include "spx-lux.h"
#include "spx-ssc.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <dirent.h>
#include <fstream>
#include <mutex>
#include <thread>
#include <time.h>
#include <unistd.h>
using android::sp;
using android::hardware::Return;
using android::hardware::Void;
using android::hardware::hidl_handle;
using android::hardware::hidl_string;
using android::hardware::hidl_vec;
namespace v1=android::hardware::sensors::V1_0;
namespace v2=android::hardware::sensors::V2_1;
using android::hardware::sensors::V2_1::implementation::IHalProxyCallback;
using android::hardware::sensors::V2_1::implementation::ISensorsSubHal;
static int64_t bootTime(){timespec t{};clock_gettime(CLOCK_BOOTTIME,&t);return int64_t(t.tv_sec)*1000000000+t.tv_nsec;}
static std::string textFile(const std::string& path){std::ifstream in(path);std::string s;std::getline(in,s);return s;}
static std::string findNamed(const char* root,const std::string& name){
 DIR*d=opendir(root);if(!d)return {};
 std::string found;
 while(auto*e=readdir(d)){
  if(e->d_name[0]=='.')continue;
  std::string p=std::string(root)+"/"+e->d_name;
  if(textFile(p+"/name")==name){found=p;break;}
 }
 closedir(d);return found;
}
class SurfaceSensors final:public ISensorsSubHal {
 static constexpr int kSensorCount=4;
 struct State {bool available=false,enabled=false;int flushes=0;int64_t period=40000000;uint64_t generation=0;};
 struct SscConfig {const char* dataType;v2::SensorType type;float minHz;float maxHz;};
 static const SscConfig& sscConfig(int index){
  static constexpr SscConfig kAccel{"accel",v2::SensorType::ACCELEROMETER,26.f,52.f};
  static constexpr SscConfig kGyro{"gyro",v2::SensorType::GYROSCOPE,26.f,52.f};
  static constexpr SscConfig kMag{"mag",v2::SensorType::MAGNETIC_FIELD,10.f,50.f};
  return index==2?kGyro:index==3?kMag:kAccel;
 }
 State state[kSensorCount];
 std::mutex mutex;
 std::condition_variable changed;
 sp<IHalProxyCallback> callback;
 std::thread workers[kSensorCount];
 bool quitting=false;
 std::string lightPath;
 std::string slpiPath;
 spx::LuxCalibration calibration;
 std::vector<v2::SensorInfo> sensors;
 bool known(int handle)const{return handle>=1&&handle<=kSensorCount&&state[handle-1].available;}
 bool slpiAlive()const{
  if(slpiPath.empty())return false;
  auto status=textFile(slpiPath+"/state");
  return status=="attached"||status=="running";
 }
 void postLocked(int index,const v2::Event* value){
  if(!callback||!state[index].enabled)return;
  std::vector<v2::Event> events;if(value)events.push_back(*value);
  while(state[index].flushes){
   v2::Event e{};e.sensorHandle=index+1;e.sensorType=v2::SensorType::META_DATA;
   e.u.meta.what=v1::MetaDataEventType::META_DATA_FLUSH_COMPLETE;
   events.push_back(e);state[index].flushes--;
  }
  if(!events.empty())callback->postEvents(events,callback->createScopedWakelock(false));
 }
 bool light(float&lux){
  const char* attrs[]={"in_intensity3_x_raw","in_intensity1_y_raw","in_intensity0_z_raw","in_intensity2_ir_raw"};
  std::array<int,4> raw{};
  for(int c=0;c<4;c++){std::ifstream in(lightPath+"/"+attrs[c]);if(!(in>>raw[c]))return false;}
  int gain=0;double integration=0;
  std::ifstream g(lightPath+"/hardwaregain"),t(lightPath+"/integration_time");
  if(!(g>>gain)||!(t>>integration))return false;
  return calibration.convert(int(std::llround(integration/0.00278))-1,gain,raw,lux);
 }
 void work(int index){
  const bool isLight=index==1;
  const SscConfig& cfg=sscConfig(index);
  spx::SscSensor ssc(cfg.dataType);
  bool connected=false,activeStream=false;int64_t configured=0,lastSent=0;
  float lastLight=NAN;uint64_t generation=UINT64_MAX;
  while(true){
   int64_t period;
   {
    std::unique_lock lock(mutex);
    if(quitting)break;
    if(!state[index].enabled){
     lastLight=NAN;lastSent=0;configured=0;
     if(!isLight&&activeStream){
      lock.unlock();
      if(slpiAlive())connected=ssc.disable(250);
      else{ssc.close(false);connected=false;}
      activeStream=false;
      lock.lock();
      continue;
     }
     changed.wait(lock,[&]{return quitting||state[index].enabled;});
     continue;
    }
    if(generation!=state[index].generation){
     generation=state[index].generation;configured=0;lastLight=NAN;lastSent=0;
     if(!isLight&&activeStream){
      lock.unlock();
      if(slpiAlive())connected=ssc.disable(250);
      else{ssc.close(false);connected=false;}
      activeStream=false;
      lock.lock();
      continue;
     }
    }
    period=state[index].period;
   }
   v2::Event event{};event.sensorHandle=index+1;bool valid=false;
   if(!isLight){
    if(!slpiAlive()){
     if(connected||ssc.isOpen())ssc.close(false);
     connected=false;activeStream=false;configured=0;
    }else{
     if(!connected){connected=ssc.open();configured=0;activeStream=false;}
     if(connected&&configured!=period){
      if(ssc.rate(std::clamp(1e9f/period,cfg.minHz,cfg.maxHz))){configured=period;activeStream=true;}
      else{ssc.close(slpiAlive());connected=false;activeStream=false;configured=0;}
     }
     if(connected){
      spx::SscSample sample{};
      int result=ssc.read(sample,100);
      if(result<0){ssc.close(slpiAlive());connected=false;activeStream=false;configured=0;}
      if(result>0){
       if(!lastSent||sample.timestamp-lastSent>=period-1000000){
        event.sensorType=cfg.type;event.timestamp=sample.timestamp;
        event.u.vec3={sample.xyz[0],sample.xyz[1],sample.xyz[2],v1::SensorStatus(sample.accuracy)};
        valid=true;lastSent=sample.timestamp;
       }
      }
     }
    }
   }else{
    float lux;
    if(light(lux)){
     event.sensorType=v2::SensorType::LIGHT;event.timestamp=bootTime();event.u.scalar=lux;
     valid=!std::isfinite(lastLight)||lux!=lastLight;lastLight=lux;
    }
   }
   std::unique_lock lock(mutex);
   if(state[index].generation==generation)postLocked(index,valid?&event:nullptr);
   if(isLight||!connected){
    int64_t delay=isLight?std::max<int64_t>(period,500000000):1000000000;
    changed.wait_for(lock,std::chrono::nanoseconds(delay),[&]{return quitting||!state[index].enabled||state[index].generation!=generation;});
   }
  }
  if(!isLight&&ssc.isOpen())ssc.close(slpiAlive());
 }
 public:
 SurfaceSensors(){
  slpiPath=findNamed("/sys/class/remoteproc","slpi");
  const bool slpiReady=slpiAlive();
  state[0].available=slpiReady;
  if(slpiReady){
   // Targeted SUID probe only; never enumerate all sensors (crashes SLPI firmware).
   // Hide gyro/mag if SLPI explicitly reports its SUID missing; if SSC is still coming up,
   // advertise alongside accel and degrade gracefully in the sensor's own worker thread.
   auto gyroProbe=spx::SscSensor("gyro").probe(250);
   if(gyroProbe==spx::SscProbeResult::Unavailable){
    state[2].available=true;
    state[3].available=true;
   }else{
    state[2].available=gyroProbe==spx::SscProbeResult::Found;
    state[3].available=spx::SscSensor("mag").probe(250)!=spx::SscProbeResult::Missing;
   }
  }
  lightPath=findNamed("/sys/bus/iio/devices","spx-tcs3430");
  state[1].available=!lightPath.empty()&&calibration.load("/vendor/etc/sensors/spx-tcs3430-golden.bin");
  state[1].period=500000000;
  for(int i=0;i<kSensorCount;i++){
   if(!state[i].available)continue;
   v2::SensorInfo s{};s.sensorHandle=i+1;s.version=1;
   if(i==0){
    s.name="Surface LSM6DSO Accelerometer";s.vendor="STMicroelectronics";
    s.type=v2::SensorType::ACCELEROMETER;s.typeAsString="android.sensor.accelerometer";
    s.maxRange=78.4532f;s.resolution=0.0023941f;s.power=0.17f;
    s.minDelay=20000;s.maxDelay=1000000;s.flags=0;
   }else if(i==1){
    s.name="Surface TCS3430 Light";s.vendor="ams OSRAM";
    s.type=v2::SensorType::LIGHT;s.typeAsString="android.sensor.light";
    s.maxRange=200000.f;s.resolution=1.f;s.power=0.15f;
    s.minDelay=0;s.maxDelay=1000000;s.flags=2;
   }else if(i==2){
    s.name="Surface LSM6DSO Gyroscope";s.vendor="STMicroelectronics";
    s.type=v2::SensorType::GYROSCOPE;s.typeAsString="android.sensor.gyroscope";
    s.maxRange=34.9066f;s.resolution=0.0010653f;s.power=0.55f;
    s.minDelay=20000;s.maxDelay=1000000;s.flags=0;
   }else{
    s.name="Surface AK0991x Magnetometer";s.vendor="AKM";
    s.type=v2::SensorType::MAGNETIC_FIELD;s.typeAsString="android.sensor.magnetic_field";
    s.maxRange=4912.f;s.resolution=0.15f;s.power=0.28f;
    s.minDelay=20000;s.maxDelay=1000000;s.flags=0;
   }
   sensors.push_back(s);
  }
  for(int i=0;i<kSensorCount;i++)if(state[i].available)workers[i]=std::thread(&SurfaceSensors::work,this,i);
 }
 ~SurfaceSensors() override{
  {std::lock_guard lock(mutex);quitting=true;changed.notify_all();}
  for(auto&t:workers)if(t.joinable())t.join();
 }
 Return<void> getSensorsList_2_1(getSensorsList_2_1_cb cb)override{cb(sensors);return Void();}
 Return<v1::Result> initialize(const sp<IHalProxyCallback>&cb)override{
  std::lock_guard lock(mutex);callback=cb;
  for(auto&s:state){s.enabled=false;s.flushes=0;s.generation++;}
  changed.notify_all();return v1::Result::OK;
 }
 Return<v1::Result> setOperationMode(v1::OperationMode mode)override{
  return mode==v1::OperationMode::NORMAL?v1::Result::OK:v1::Result::BAD_VALUE;
 }
 Return<v1::Result> activate(int32_t h,bool enabled)override{
  std::lock_guard lock(mutex);if(!known(h))return v1::Result::BAD_VALUE;
  auto&s=state[h-1];if(s.enabled!=enabled){s.enabled=enabled;s.flushes=0;s.generation++;}
  changed.notify_all();return v1::Result::OK;
 }
 Return<v1::Result> batch(int32_t h,int64_t period,int64_t latency)override{
  std::lock_guard lock(mutex);if(!known(h)||period<0||latency<0)return v1::Result::BAD_VALUE;
  state[h-1].period=std::clamp<int64_t>(period,h==2?500000000:20000000,1000000000);
  changed.notify_all();return v1::Result::OK;
 }
 Return<v1::Result> flush(int32_t h)override{
  std::lock_guard lock(mutex);if(!known(h)||!state[h-1].enabled)return v1::Result::BAD_VALUE;
  state[h-1].flushes++;changed.notify_all();return v1::Result::OK;
 }
 Return<v1::Result> injectSensorData_2_1(const v2::Event&)override{return v1::Result::INVALID_OPERATION;}
 Return<void> registerDirectChannel(const v1::SharedMemInfo&,registerDirectChannel_cb cb)override{cb(v1::Result::INVALID_OPERATION,-1);return Void();}
 Return<v1::Result> unregisterDirectChannel(int32_t)override{return v1::Result::INVALID_OPERATION;}
 Return<void> configDirectReport(int32_t,int32_t,v1::RateLevel,configDirectReport_cb cb)override{cb(v1::Result::INVALID_OPERATION,0);return Void();}
 Return<void> debug(const hidl_handle& fd,const hidl_vec<hidl_string>&)override{
  std::lock_guard lock(mutex);auto*h=fd.getNativeHandle();
  if(h&&h->numFds)dprintf(h->data[0],"SPX sensors: accel=%d light=%d gyro=%d mag=%d active=%d,%d,%d,%d\n",
   state[0].available,state[1].available,state[2].available,state[3].available,
   state[0].enabled,state[1].enabled,state[2].enabled,state[3].enabled);
  return Void();
 }
 const std::string getName()override{return "Surface Pro X sensors";}
};
extern "C" ISensorsSubHal* sensorsHalGetSubHal_2_1(uint32_t*version){*version=SUB_HAL_2_1_VERSION;static SurfaceSensors hal;return &hal;}
