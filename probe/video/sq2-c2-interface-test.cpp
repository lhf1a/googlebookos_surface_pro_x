#include "sq2-c2-interface.h"
int main(){
 setvbuf(stdout,nullptr,_IONBF,0);
 auto reflector=std::make_shared<C2ReflectorHelper>();
 for(auto codec:{Sq2Codec::Vp8,Sq2Codec::Vp9}){
  auto vpx=std::make_shared<Sq2DecodeInterface>(reflector,96,4096,96,4096,codec);
  android::SimpleC2Interface<Sq2DecodeInterface> vi(sq2CodecInfo(codec).name,0,vpx);
  C2StreamProfileLevelInfo::input p(0u);
  std::vector<std::unique_ptr<C2Param>> h;
  assert(vi.query_vb({&p},{},C2_MAY_BLOCK,&h)==C2_OK);
  assert(p.profile==(codec==Sq2Codec::Vp8?C2Config::PROFILE_VP8_0:C2Config::PROFILE_VP9_0));
  std::vector<std::unique_ptr<C2SettingResult>> errors;
  assert(vi.config_vb({&p},C2_MAY_BLOCK,&errors)==C2_OK&&errors.empty());
  p.profile=codec==Sq2Codec::Vp8?C2Config::PROFILE_VP8_1:C2Config::PROFILE_VP9_2;
  assert(vi.config_vb({&p},C2_MAY_BLOCK,&errors)!=C2_OK&&!errors.empty());
  h.clear();assert(vi.query_vb({&p},{},C2_MAY_BLOCK,&h)==C2_OK);
  assert(p.profile==(codec==Sq2Codec::Vp8?C2Config::PROFILE_VP8_0:C2Config::PROFILE_VP9_0));
  printf("VPX_PROFILE_ACCEPT_REJECT_PASS %s\n",sq2CodecInfo(codec).name);
 }
 // Explicit test fixture bounds, not a substitute for a device capability query.
 auto impl=std::make_shared<Sq2DecodeInterface>(reflector,320,640,240,480);
 android::SimpleC2Interface<Sq2DecodeInterface> intf("c2.sq2.avc.decoder.experimental",0,impl);
 std::vector<std::shared_ptr<C2ParamDescriptor>> supported;
 auto ret=intf.querySupportedParams_nb(&supported);printf("SUPPORTED ret=%d count=%zu\n",ret,supported.size());assert(ret==C2_OK&&supported.size()==10);
 C2StreamProfileLevelInfo::input profile(0u,C2Config::PROFILE_UNUSED,C2Config::LEVEL_UNUSED);
 std::vector<std::unique_ptr<C2Param>> profileHeap;
 ret=intf.query_vb({&profile},{},C2_MAY_BLOCK,&profileHeap);
 assert(ret==C2_OK&&profile.profile==C2Config::PROFILE_AVC_CONSTRAINED_BASELINE&&profile.level==C2Config::LEVEL_AVC_5_2);
 std::vector<std::unique_ptr<C2SettingResult>> profileFailures;
 profile.level=C2Config::LEVEL_AVC_5_1;
 ret=intf.config_vb({&profile},C2_MAY_BLOCK,&profileFailures);
 assert(ret==C2_OK&&profileFailures.empty());
 profile.profile=C2Config::PROFILE_AVC_HIGH;
 ret=intf.config_vb({&profile},C2_MAY_BLOCK,&profileFailures);
 assert(ret==C2_OK&&profileFailures.empty());
 profile.profile=C2Config::PROFILE_AVC_HIGH_10;
 ret=intf.config_vb({&profile},C2_MAY_BLOCK,&profileFailures);
 assert(ret!=C2_OK&&!profileFailures.empty());
 puts("PROFILE_LEVEL_CONFIG_PASS");
 C2StreamPictureSizeInfo::output size(0u,0,0);
 std::vector<std::unique_ptr<C2Param>> heap;
 ret=intf.query_vb({&size},{},C2_MAY_BLOCK,&heap);printf("DEFAULT ret=%d size=%ux%u\n",ret,size.width,size.height);assert(ret==C2_OK&&size.width==320&&size.height==240);
 size.width=640;size.height=480;std::vector<std::unique_ptr<C2SettingResult>> failures;
 ret=intf.config_vb({&size},C2_MAY_BLOCK,&failures);printf("VALID ret=%d failures=%zu\n",ret,failures.size());assert(ret==C2_OK&&failures.empty());
 size.width=641;failures.clear();ret=intf.config_vb({&size},C2_MAY_BLOCK,&failures);printf("INVALID ret=%d failures=%zu\n",ret,failures.size());assert(ret!=C2_OK&&!failures.empty());
 C2StreamPictureSizeInfo::output actual(0u,0,0);ret=intf.query_vb({&actual},{},C2_MAY_BLOCK,&heap);printf("AFTER_INVALID ret=%d size=%ux%u\n",ret,actual.width,actual.height);assert(ret==C2_OK&&actual.width==640&&actual.height==480);
 C2BlockPool::local_id_t poolIds[1]={1234};
 auto pools=C2PortBlockPoolsTuning::output::AllocUnique(poolIds);failures.clear();
 ret=intf.config_vb({pools.get()},C2_MAY_BLOCK,&failures);assert(ret==C2_OK&&failures.empty());
 heap.clear();ret=intf.query_vb({}, {C2PortBlockPoolsTuning::output::PARAM_TYPE},C2_MAY_BLOCK,&heap);
 assert(ret==C2_OK&&heap.size()==1);auto result=C2PortBlockPoolsTuning::output::From(heap[0].get());
 assert(result&&result->flexCount()==1&&result->m.values[0]==1234);
 puts("POOL_ID_ROUNDTRIP_PASS");puts("INTERFACE_TEST_PASS");return 0;
}
