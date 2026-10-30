#include <aidl/android/hardware/media/c2/IComponentStore.h>
#include <aidl/android/hardware/media/c2/IComponentInterface.h>
#include <aidl/android/hardware/media/c2/IConfigurable.h>
#include <android/binder_manager.h>
#include <cstdio>
int main(){
 setvbuf(stdout,nullptr,_IONBF,0);using Store=aidl::android::hardware::media::c2::IComponentStore;
 ndk::SpAIBinder binder(AServiceManager_checkService("android.hardware.media.c2.IComponentStore/default1"));if(!binder.get()){puts("SERVICE_ABSENT");return 2;}
 auto store=Store::fromBinder(binder);if(!store)return 3;int version=0;std::string hash;auto v=store->getInterfaceVersion(&version);auto h=store->getInterfaceHash(&hash);printf("REMOTE_INTERFACE version=%d hash=%s ok=%d\n",version,hash.c_str(),v.isOk()&&h.isOk());
 std::vector<Store::ComponentTraits> traits;auto s=store->listComponents(&traits);printf("REMOTE_LIST ok=%d count=%zu\n",s.isOk(),traits.size());if(!s.isOk()||traits.size()!=1)return 4;
 for(auto& t:traits)printf("REMOTE_COMPONENT name=%s mime=%s\n",t.name.c_str(),t.mediaType.c_str());
 std::shared_ptr<aidl::android::hardware::media::c2::IComponentInterface> i;s=store->createInterface("c2.sq2.avc.experimental",&i);printf("REMOTE_CREATE_INTERFACE ok=%d nonnull=%d\n",s.isOk(),!!i);if(!s.isOk()||!i)return 5;
 std::shared_ptr<aidl::android::hardware::media::c2::IConfigurable> config;s=i->getConfigurable(&config);printf("REMOTE_CONFIGURABLE ok=%d nonnull=%d\n",s.isOk(),!!config);if(!s.isOk()||!config)return 6;
 puts("AIDL_DISCOVERY_TEST_PASS");return 0;
}
