#include <C2Component.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <cstdint>
int main(int argc,char** argv) {
 bool p010=argc==2&&!strcmp(argv[1],"--p010");
 if(argc!=1&&!p010)return 2;
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
 ret=allocator->newGraphicAllocation(320,height,p010?0x36:0x23,usage,&graphic);
 printf("ALLOC_GRAPHIC=%d\n",ret);if(ret||!graphic)return 10;
 C2PlanarLayout layout{};uint8_t *planes[C2PlanarLayout::MAX_NUM_PLANES]{};
 C2Rect rect(320,height);
 ret=graphic->map(rect,usage,nullptr,&layout,planes);
 printf("MAP_GRAPHIC=%d planes=%u\n",ret,layout.numPlanes);if(ret)return 11;
 for(unsigned i=0;i<layout.numPlanes && i<C2PlanarLayout::MAX_NUM_PLANES;i++)
  printf("PLANE=%u row=%d col=%d depth=%u allocated=%u shift=%u endian=%u\n",i,layout.planes[i].rowInc,layout.planes[i].colInc,layout.planes[i].bitDepth,layout.planes[i].allocatedDepth,layout.planes[i].rightShift,unsigned(layout.planes[i].endianness));
 auto pixels=[&](bool write)->bool {
  if(layout.type!=C2PlanarLayout::TYPE_YUV||layout.numPlanes!=3)return false;
  for(unsigned plane=0;plane<3;plane++){
   const auto& l=layout.planes[plane];unsigned w=plane?160:320,h=plane?height/2:height;
   if(!planes[plane]||l.bitDepth!=10||l.allocatedDepth!=16||l.rightShift!=6||
      l.endianness==C2PlaneInfo::BIG_END||l.colInc<2||l.rowInc<=0||
      uint64_t(l.rowInc)<uint64_t(w-1)*l.colInc+2||
      l.colSampling!=(plane?2u:1u)||l.rowSampling!=(plane?2u:1u))return false;
   for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
    uint16_t expected=uint16_t(((x*7+y*13+plane*101)&1023)<<6),actual;
    auto at=planes[plane]+size_t(y)*l.rowInc+size_t(x)*l.colInc;
    if(write)memcpy(at,&expected,2);else{memcpy(&actual,at,2);if(actual!=expected)return false;}
   }
  }
  return true;
 };
 if(p010&&!pixels(true)){puts("P010_LAYOUT_OR_WRITE_FAILED");return 15;}
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
 if(ret)return 14;
 if(p010){
  ret=graphic->map(rect,usage,nullptr,&layout,planes);
  if(ret||!pixels(false)){puts("P010_REMAP_READBACK_FAILED");return 16;}
  ret=graphic->unmap(planes,rect,nullptr);if(ret)return 17;
  puts("P010_10BIT_REMAP_READBACK_OK");
 }
 graphic.reset();
 }
 allocator.reset();store.reset();puts("RELEASE_DONE");
 return ret||sum!=4096*0x5a;
}
