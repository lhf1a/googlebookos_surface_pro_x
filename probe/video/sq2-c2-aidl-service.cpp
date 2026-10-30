#include "sq2-c2-interface.h"
#include "sq2-c2-v4l2-component.h"
#include <codec2/aidl/ComponentStore.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android/binder_stability.h>
#include <cstdio>
#include <signal.h>
#include <dlfcn.h>
#include <unwind.h>
static _Unwind_Reason_Code trace(_Unwind_Context* c,void*){auto pc=_Unwind_GetIP(c);Dl_info d{};dladdr((void*)pc,&d);dprintf(2,"TRACE %s %llx\n",d.dli_fname?d.dli_fname:"?",(unsigned long long)(pc-(uintptr_t)d.dli_fbase));return _URC_NO_REASON;}
#include <ucontext.h>
#include <dlfcn.h>
static void crash(int,siginfo_t* info,void* context){auto u=(ucontext_t*)context;Dl_info d{};dladdr((void*)u->uc_mcontext.pc,&d);dprintf(2,"FAULT addr=%p pc=%llx module=%s offset=%llx\n",info->si_addr,(unsigned long long)u->uc_mcontext.pc,d.dli_fname?d.dli_fname:"?",(unsigned long long)(u->uc_mcontext.pc-(uintptr_t)d.dli_fbase));_Unwind_Backtrace(trace,nullptr);_exit(139);}
class Sq2Store final:public C2ComponentStore{
 class Params:public C2InterfaceHelper{public:Params(const std::shared_ptr<C2ReflectorHelper>& r):C2InterfaceHelper(r){setDerivedInstance(this);}};
 std::shared_ptr<C2ReflectorHelper> reflector_=std::make_shared<C2ReflectorHelper>();Params params_{reflector_};
public:
 C2String getName()const override{return "sq2.experimental";}
 c2_status_t createInterface(C2String name,std::shared_ptr<C2ComponentInterface>* out)override{
  const Sq2CodecInfo* info=nullptr;
  for(const auto& candidate:sq2Codecs)if(sq2CodecMatches(candidate,name.c_str()))info=&candidate;
  if(!info){out->reset();return C2_NOT_FOUND;}
  // Current 8-bit buffer envelope; firmware's larger sizes require more work.
  auto impl=std::make_shared<Sq2DecodeInterface>(reflector_,96,4096,96,4096,info->codec);
  *out=std::make_shared<android::SimpleC2Interface<Sq2DecodeInterface>>(info->name,0,impl);return C2_OK;
 }
 c2_status_t createComponent(C2String name,std::shared_ptr<C2Component>* out)override{
  std::shared_ptr<C2ComponentInterface> i;auto ret=createInterface(name,&i);
  if(ret){out->reset();return ret;}
  for(const auto& info:sq2Codecs)if(sq2CodecMatches(info,name.c_str())){*out=std::make_shared<Sq2C2V4L2Component>(i,info.codec);return C2_OK;}
  out->reset();return C2_NOT_FOUND;
 }
 std::vector<std::shared_ptr<const C2Component::Traits>> listComponents()override{
  std::vector<std::shared_ptr<const C2Component::Traits>> result;
  for(const auto& info:sq2Codecs){
   auto t=std::make_shared<C2Component::Traits>();t->name=info.name;
   t->aliases.push_back(sq2CodecAlias(info.codec));
   t->domain=C2Component::DOMAIN_VIDEO;t->kind=C2Component::KIND_DECODER;
   t->rank=512;t->mediaType=info.mime;t->owner=getName();result.push_back(t);
  }
  return result;
 }
 c2_status_t copyBuffer(std::shared_ptr<C2GraphicBuffer>,std::shared_ptr<C2GraphicBuffer>)override{return C2_OMITTED;}
 c2_status_t query_sm(const std::vector<C2Param*>& a,const std::vector<C2Param::Index>& b,std::vector<std::unique_ptr<C2Param>>* c)const override{return params_.query(a,b,C2_MAY_BLOCK,c);}
 c2_status_t config_sm(const std::vector<C2Param*>& a,std::vector<std::unique_ptr<C2SettingResult>>* b)override{return params_.config(a,C2_MAY_BLOCK,b);}
 std::shared_ptr<C2ParamReflector> getParamReflector()const override{return reflector_;}
 c2_status_t querySupportedParams_nb(std::vector<std::shared_ptr<C2ParamDescriptor>>* p)const override{return params_.querySupportedParams(p);}
 c2_status_t querySupportedValues_sm(std::vector<C2FieldSupportedValuesQuery>& f)const override{return params_.querySupportedValues(f,C2_MAY_BLOCK);}
};
int main(int argc,char** argv){
 if(argc==2&&!strcmp(argv[1],"--names-test")){
  Sq2Store store;
  for(const auto& info:sq2Codecs){
   for(const char* name:{info.name,sq2CodecAlias(info.codec)}){
    std::shared_ptr<C2ComponentInterface> intf;
    if(store.createInterface(name,&intf)!=C2_OK||intf->getName()!=info.name)return 6;
    std::shared_ptr<C2Component> component;
    if(store.createComponent(name,&component)!=C2_OK||!component)return 7;
    printf("NAME_OK requested=%s canonical=%s\n",name,intf->getName().c_str());
   }
   std::shared_ptr<C2ComponentInterface> rejected;
   if(store.createInterface(C2String(sq2CodecAlias(info.codec))+".secure",&rejected)!=C2_NOT_FOUND)return 8;
  }
  for(const auto& t:store.listComponents())if(t->aliases.size()!=1)return 9;
  puts("NAMES_PASS codecs=4 names=8 secure_rejected=4 no_decode=1");return 0;
 }
 setvbuf(stdout,nullptr,_IONBF,0);struct sigaction sa{};sa.sa_sigaction=crash;sa.sa_flags=SA_SIGINFO;sigaction(SIGSEGV,&sa,nullptr);ABinderProcess_setThreadPoolMaxThreadCount(4);ABinderProcess_startThreadPool();
 using Store=aidl::android::hardware::media::c2::utils::ComponentStore;
 static_assert(sizeof(Store)==352,"device provider wrapper size differs");
 auto store=ndk::SharedRefBase::make<Store>(std::make_shared<Sq2Store>());
 printf("STORE_STATUS=%d\n",store->status());if(store->status()!=C2_OK)return 2;
 std::vector<aidl::android::hardware::media::c2::IComponentStore::ComponentTraits> traits;
 auto result=store->listComponents(&traits);printf("LOCAL_LIST ok=%d count=%zu\n",result.isOk(),traits.size());if(!result.isOk()||traits.size()!=sizeof(sq2Codecs)/sizeof(sq2Codecs[0]))return 3;
 for(auto& t:traits)printf("COMPONENT name=%s mime=%s\n",t.name.c_str(),t.mediaType.c_str());
 if(argc==1)return 0;
 bool local=argc==2&&!strcmp(argv[1],"--serve-local");
 if(argc!=2||(!local&&strcmp(argv[1],"--serve")))return 4;
 puts("BINDER_BEGIN");auto binder=store->asBinder();puts("BINDER_DONE");
 if(local){AIBinder_forceDowngradeToLocalStability(binder.get());puts("LAB_LOCAL_STABILITY_NOT_VINTF");}
 auto rc=AServiceManager_addService(binder.get(),"android.hardware.media.c2.IComponentStore/default1");printf("REGISTER_STATUS=%d\n",rc);if(rc!=STATUS_OK)return 5;
 ABinderProcess_joinThreadPool();return 0;
}
