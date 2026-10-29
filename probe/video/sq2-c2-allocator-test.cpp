#include <C2Component.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <cstdint>
int main() {
 setvbuf(stdout,nullptr,_IONBF,0); alarm(15);
 void *lib=dlopen("libcodec2_vndk.so",RTLD_NOW|RTLD_LOCAL);
 if(!lib){printf("DLOPEN_ERROR=%s\n",dlerror());return 2;}
 using Factory=std::shared_ptr<C2AllocatorStore>(*)();
 auto get=reinterpret_cast<Factory>(dlsym(lib,"_ZN7android31GetCodec2PlatformAllocatorStoreEv"));
 if(!get){printf("SYMBOL_ERROR=%s\n",dlerror());return 3;}
 auto store=get();if(!store)return 4;
 puts("STORE_OK");
 std::shared_ptr<C2Allocator> allocator;
 auto ret=store->fetchAllocator(C2AllocatorStore::DEFAULT_LINEAR,&allocator);
 printf("FETCH_LINEAR=%d\n",ret);if(ret||!allocator)return 5;
 C2MemoryUsage usage{C2MemoryUsage::CPU_READ|C2MemoryUsage::CPU_WRITE};
 std::shared_ptr<C2LinearAllocation> allocation;
 ret=allocator->newLinearAllocation(4096,usage,&allocation);
 printf("ALLOC_LINEAR=%d\n",ret);if(ret||!allocation)return 6;
 void *ptr=nullptr;ret=allocation->map(0,4096,usage,nullptr,&ptr);
 printf("MAP_LINEAR=%d\n",ret);if(ret||!ptr)return 7;
 memset(ptr,0x5a,4096);unsigned sum=0;for(unsigned i=0;i<4096;i++)sum+=static_cast<unsigned char*>(ptr)[i];
 printf("READBACK_SUM=%u\n",sum);
 ret=allocation->unmap(ptr,4096,nullptr);printf("UNMAP_LINEAR=%d\n",ret);
 allocation.reset();allocator.reset();
 if(ret||sum!=4096*0x5a)return 8;
 ret=store->fetchAllocator(C2AllocatorStore::DEFAULT_GRAPHIC,&allocator);
 printf("FETCH_GRAPHIC=%d\n",ret);if(ret||!allocator)return 9;
 for(unsigned height : {240u,256u,288u}) {
 printf("GRAPHIC_HEIGHT=%u\n",height);
 std::shared_ptr<C2GraphicAllocation> graphic;
 ret=allocator->newGraphicAllocation(320,height,0x23,usage,&graphic);
 printf("ALLOC_GRAPHIC=%d\n",ret);if(ret||!graphic)return 10;
 C2PlanarLayout layout{};uint8_t *planes[C2PlanarLayout::MAX_NUM_PLANES]{};
 C2Rect rect(320,height);
 ret=graphic->map(rect,usage,nullptr,&layout,planes);
 printf("MAP_GRAPHIC=%d planes=%u\n",ret,layout.numPlanes);if(ret)return 11;
 for(unsigned i=0;i<layout.numPlanes && i<C2PlanarLayout::MAX_NUM_PLANES;i++)
  printf("PLANE=%u row=%d col=%d depth=%u\n",i,layout.planes[i].rowInc,layout.planes[i].colInc,layout.planes[i].bitDepth);
 using Unwrap=native_handle_t*(*)(const C2Handle*);
 auto unwrap=reinterpret_cast<Unwrap>(dlsym(lib,"_ZN7android31UnwrapNativeCodec2GrallocHandleEPK13native_handle"));
 if(!unwrap)return 12;
 auto handle=unwrap(graphic->handle());if(!handle)return 13;
 printf("NATIVE_HANDLE fds=%d ints=%d\n",handle->numFds,handle->numInts);
 for(int i=0;i<handle->numFds;i++){
  auto length=lseek(handle->data[i],0,SEEK_END);
  printf("FD_SIZE index=%d bytes=%lld errno=%d\n",i,(long long)length,length<0?errno:0);
  if(length>=0)lseek(handle->data[i],0,SEEK_SET);
 }
 // Unwrap returns a malloc-backed non-owning native handle. Never close its borrowed fds.
 free(handle);
 if(layout.numPlanes==3){
  printf("UV_OFFSETS u=%lld v=%lld\n",(long long)((uintptr_t)planes[1]-(uintptr_t)planes[0]),(long long)((uintptr_t)planes[2]-(uintptr_t)planes[0]));
 }
 ret=graphic->unmap(planes,rect,nullptr);printf("UNMAP_GRAPHIC=%d\n",ret);
 graphic.reset();if(ret)return 14;
 }
 allocator.reset();store.reset();puts("RELEASE_DONE");
 return ret||sum!=4096*0x5a;
}
