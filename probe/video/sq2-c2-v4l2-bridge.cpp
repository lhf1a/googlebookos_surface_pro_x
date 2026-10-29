// Lab bridge: retain real C2 gralloc allocations while an isolated V4L2 child decodes.
#include <C2Component.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <cerrno>
int main(int argc,char **argv){
 setvbuf(stdout,nullptr,_IONBF,0);
 if(argc!=2)return 2;
 auto lib=dlopen("libcodec2_vndk.so",RTLD_NOW|RTLD_LOCAL);if(!lib)return 3;
 using Factory=std::shared_ptr<C2AllocatorStore>(*)();
 using Unwrap=native_handle_t*(*)(const C2Handle*);
 auto get=(Factory)dlsym(lib,"_ZN7android31GetCodec2PlatformAllocatorStoreEv");
 auto unwrap=(Unwrap)dlsym(lib,"_ZN7android31UnwrapNativeCodec2GrallocHandleEPK13native_handle");
 if(!get||!unwrap)return 4;
 auto store=get();if(!store)return 5;
 std::shared_ptr<C2Allocator> allocator;
 if(store->fetchAllocator(C2AllocatorStore::DEFAULT_GRAPHIC,&allocator)||!allocator)return 6;
 std::shared_ptr<C2GraphicAllocation> buffers[9];
 C2MemoryUsage usage{C2MemoryUsage::CPU_READ|C2MemoryUsage::CPU_WRITE};
 for(int i=0;i<9;i++){
  if(fcntl(200+i,F_GETFD)!=-1||errno!=EBADF)return 7;
  if(allocator->newGraphicAllocation(320,240,0x23,usage,&buffers[i])||!buffers[i])return 8;
  C2PlanarLayout layout{};uint8_t *p[C2PlanarLayout::MAX_NUM_PLANES]{};C2Rect rect(320,240);
  if(buffers[i]->map(rect,usage,nullptr,&layout,p))return 9;
  bool valid=layout.type==C2PlanarLayout::TYPE_YUV&&layout.numPlanes==3;
  for(int j=0;j<3&&valid;j++)valid=p[j]&&layout.planes[j].rowInc==384&&layout.planes[j].colInc==(j?2:1)&&layout.planes[j].bitDepth==8;
  valid=valid&&((uintptr_t)p[1]-(uintptr_t)p[0]==98304)&&((uintptr_t)p[2]-(uintptr_t)p[0]==98305);
  if(buffers[i]->unmap(p,rect,nullptr)||!valid)return 10;
  auto h=unwrap(buffers[i]->handle());if(!h)return 11;
  struct stat a{},b{};
  valid=h->numFds>=2&&!fstat(h->data[0],&a)&&!fstat(h->data[1],&b)&&a.st_dev==b.st_dev&&a.st_ino==b.st_ino&&lseek(h->data[0],0,SEEK_END)==159744;
  if(valid){lseek(h->data[0],0,SEEK_SET);valid=dup2(h->data[0],200+i)==200+i;}
  free(h);if(!valid)return 12;
  printf("C2_READY index=%d bytes=159744 uv=98304 stride=384 borrowed_fd=%d\n",i,200+i);
 }
 fflush(stdout);fsync(1);
 auto pid=fork();if(pid<0)return 13;
 if(!pid){execl(argv[1],argv[1],"--graphic-fds","/dev/video0",(char*)nullptr);_exit(127);}
 int status=0;pid_t waited;do{waited=waitpid(pid,&status,0);}while(waited<0&&errno==EINTR);
 if(waited!=pid)return 14;
 for(int i=0;i<9;i++){close(200+i);buffers[i].reset();}
 allocator.reset();store.reset();
 printf("C2_RELEASE_DONE child_status=%d\n",status);fsync(1);
 return WIFEXITED(status)?WEXITSTATUS(status):15;
}
