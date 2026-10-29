#include "sq2-c2-interface.h"
int main(){
 setvbuf(stdout,nullptr,_IONBF,0);
 auto reflector=std::make_shared<C2ReflectorHelper>();
 // Explicit test fixture bounds, not a substitute for a device capability query.
 auto impl=std::make_shared<Sq2DecodeInterface>(reflector,320,640,240,480);
 android::SimpleC2Interface<Sq2DecodeInterface> intf("c2.sq2.avc.decoder.experimental",0,impl);
 std::vector<std::shared_ptr<C2ParamDescriptor>> supported;
 auto ret=intf.querySupportedParams_nb(&supported);printf("SUPPORTED ret=%d count=%zu\n",ret,supported.size());assert(ret==C2_OK&&supported.size()==5);
 C2StreamPictureSizeInfo::output size(0u,0,0);
 std::vector<std::unique_ptr<C2Param>> heap;
 ret=intf.query_vb({&size},{},C2_MAY_BLOCK,&heap);printf("DEFAULT ret=%d size=%ux%u\n",ret,size.width,size.height);assert(ret==C2_OK&&size.width==320&&size.height==240);
 size.width=640;size.height=480;std::vector<std::unique_ptr<C2SettingResult>> failures;
 ret=intf.config_vb({&size},C2_MAY_BLOCK,&failures);printf("VALID ret=%d failures=%zu\n",ret,failures.size());assert(ret==C2_OK&&failures.empty());
 size.width=641;failures.clear();ret=intf.config_vb({&size},C2_MAY_BLOCK,&failures);printf("INVALID ret=%d failures=%zu\n",ret,failures.size());assert(ret!=C2_OK&&!failures.empty());
 C2StreamPictureSizeInfo::output actual(0u,0,0);ret=intf.query_vb({&actual},{},C2_MAY_BLOCK,&heap);printf("AFTER_INVALID ret=%d size=%ux%u\n",ret,actual.width,actual.height);assert(ret==C2_OK&&actual.width==640&&actual.height==480);
 puts("INTERFACE_TEST_PASS");return 0;
}
