// Bounded allocation-only throughput probe; no V4L2, PAS or decoder calls.
#include <C2Component.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <chrono>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <linux/dma-heap.h>
#include <linux/dma-buf.h>
#include "sq2-split-uv.h"
static bool transfer(const char* label,uint8_t* dst,const uint8_t* src,size_t bytes){
 auto begin=std::chrono::steady_clock::now();
 for(int n=0;n<8;++n){rusage before{},after{};getrusage(RUSAGE_SELF,&before);auto round=std::chrono::steady_clock::now();sq2_copy_row(dst,src,bytes);
  auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-round).count();getrusage(RUSAGE_SELF,&after);
  printf("ROUND %s n=%d us=%lld minor_faults=%ld\n",label,n,(long long)us,after.ru_minflt-before.ru_minflt);}
 auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-begin).count();
 bool good=true;for(size_t n=0;n<bytes;++n)if(dst[n]!=0x5a){good=false;break;}
 printf("COPY %s bytes=%zu rounds=8 us=%lld correct=%d\n",label,bytes,(long long)us,good);return good;
}
int main(){
 setvbuf(stdout,nullptr,_IONBF,0);alarm(25);
 void* mapper=dlopen("/vendor/lib64/hw/mapper.minigbm.so",RTLD_NOW|RTLD_LOCAL);
 printf("MAPPER_LOAD %s\n",mapper?"ok":dlerror());if(!mapper)return 17;
 constexpr size_t bytes=3840*2160;
 std::vector<uint8_t> a(bytes,0x5a),b(bytes,0x5a);
 bool good=transfer("cached-to-cached",b.data(),a.data(),bytes);
 int heap=open("/dev/dma_heap/system",O_RDWR|O_CLOEXEC);if(heap<0)return 2;
 dma_heap_allocation_data alloc{};alloc.len=bytes;alloc.fd_flags=O_RDWR|O_CLOEXEC;
 if(ioctl(heap,DMA_HEAP_IOCTL_ALLOC,&alloc))return 3;close(heap);
 auto mem=static_cast<uint8_t*>(mmap(nullptr,bytes,PROT_READ|PROT_WRITE,MAP_SHARED,alloc.fd,0));if(mem==MAP_FAILED)return 4;
 dma_buf_sync sync{};sync.flags=DMA_BUF_SYNC_START|DMA_BUF_SYNC_RW;
 if(ioctl(alloc.fd,DMA_BUF_IOCTL_SYNC,&sync))return 5;
 memset(mem,0x5a,bytes);
 good&=transfer("heap-to-cached",b.data(),mem,bytes);
 good&=transfer("cached-to-heap",mem,a.data(),bytes);
 void* lib=dlopen("libcodec2_vndk.so",RTLD_NOW|RTLD_LOCAL);if(!lib){puts(dlerror());return 6;}
 using Factory=std::shared_ptr<C2AllocatorStore>(*)();
 auto factory=reinterpret_cast<Factory>(dlsym(lib,"_ZN7android31GetCodec2PlatformAllocatorStoreEv"));if(!factory)return 7;
 auto store=factory();std::shared_ptr<C2Allocator> allocator;
 if(!store||store->fetchAllocator(C2AllocatorStore::DEFAULT_GRAPHIC,&allocator))return 8;
 C2MemoryUsage usage{C2MemoryUsage::CPU_READ|C2MemoryUsage::CPU_WRITE};std::shared_ptr<C2GraphicAllocation> graphic;
 if(allocator->newGraphicAllocation(3840,2160,0x23,usage,&graphic))return 9;
 C2PlanarLayout layout{};uint8_t* p[C2PlanarLayout::MAX_NUM_PLANES]{};C2Rect rect(3840,2160);
 if(graphic->map(rect,usage,nullptr,&layout,p))return 10;
 printf("Y_LAYOUT row=%d col=%d usage=%llu\n",layout.planes[0].rowInc,layout.planes[0].colInc,(unsigned long long)usage.expected);
 if(layout.numPlanes!=3||layout.planes[0].rowInc!=3840||layout.planes[0].colInc!=1)return 11;
 good&=transfer("cached-to-gralloc",p[0],a.data(),bytes);
 good&=transfer("gralloc-to-cached",b.data(),p[0],bytes);
 good&=transfer("heap-to-gralloc",p[0],mem,bytes);
 if(graphic->unmap(p,rect,nullptr))return 12;
 for(int repeat=0;repeat<2;++repeat){
  if(graphic->map(rect,usage,nullptr,&layout,p))return 15;
  good&=transfer("remapped-gralloc",p[0],a.data(),bytes);
  if(graphic->unmap(p,rect,nullptr))return 16;
 }
 graphic.reset();allocator.reset();store.reset();
 sync.flags=DMA_BUF_SYNC_END|DMA_BUF_SYNC_RW;if(ioctl(alloc.fd,DMA_BUF_IOCTL_SYNC,&sync))return 13;
 munmap(mem,bytes);close(alloc.fd);puts(good?"MEMORY_PROBE_PASS":"MEMORY_PROBE_FAIL");return good?0:14;
}
