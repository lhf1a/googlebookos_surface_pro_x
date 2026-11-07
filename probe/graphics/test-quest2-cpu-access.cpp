/* Host fixtures execute the actual candidate functions and metadata parser.
 * Only platform/kernel calls are replaced. Not GPU or cache-coherency proof. */
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <vector>
#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/dma-buf.h>
#include <linux/sync_file.h>
#include "vm_imapper5.h"
static void *fixture_dlopen(const char *, int);
static void *fixture_dlsym(void *, const char *);
static int fixture_ioctl(int, unsigned long, ...);
static int fixture_fcntl(int, int, ...);
static int fixture_close(int);
static int fixture_fstat(int, struct stat *);
static int fixture_poll(pollfd *, nfds_t, int);
static void *fixture_mmap(void *, size_t, int, int, int, off_t);
static int fixture_munmap(void *, size_t);
#define dlopen fixture_dlopen
#define dlsym fixture_dlsym
#define ioctl fixture_ioctl
#define fcntl fixture_fcntl
#define close fixture_close
#define fstat fixture_fstat
#define poll fixture_poll
#define mmap fixture_mmap
#define munmap fixture_munmap
#include "quest2-cpu-access.cpp"
#undef dlopen
#undef dlsym
#undef ioctl
#undef fcntl
#undef close
#undef fstat
#undef poll
#undef mmap
#undef munmap

static AIMapper platform;
static std::set<int> fds;
static unsigned normal_locks, normal_unlocks, normal_frees, starts, ends, unmaps, polls;
static int next_fd, lock_result, ioctl_failure, fence_failure;
static bool dup_failure, map_failure, metadata_failure;
static uint64_t allocation_usage, modifier, layers, offset, direction_seen;
static uint32_t format;
alignas(native_handle_t) static unsigned char handle_storage[sizeof(native_handle_t) + sizeof(int)];
static buffer_handle_t handle;
static void *mapped;
static void append(std::vector<unsigned char> &v, const void *p, size_t n) {
    auto *b = static_cast<const unsigned char *>(p); v.insert(v.end(), b, b+n);
}
static void u64(std::vector<unsigned char> &v, uint64_t n) { append(v, &n, 8); }
static void string(std::vector<unsigned char> &v, const char *s) { u64(v, std::strlen(s)); append(v,s,std::strlen(s)); }
static int32_t metadata(buffer_handle_t, int64_t key, void *data, size_t capacity) {
    if (metadata_failure) return -AIMAPPER_ERROR_BAD_BUFFER;
    std::vector<unsigned char> v;
    string(v, "android.hardware.graphics.common.StandardMetadataType"); u64(v,key);
    uint64_t value = 0;
    switch (key) {
    case 1: value=7;break; case 3: case 4:value=64;break; case 5:value=layers;break;
    case 6:append(v,&format,4);break;
    case 7:{uint32_t f=0x34324241;append(v,&f,4);break;}
    case 8:value=modifier;break;case 9:value=allocation_usage;break;case 10:value=16384;break;
    case 15:{
        u64(v,1);u64(v,4);const uint64_t types[]={1024,2048,4096,1073741824};
        for (unsigned i=0;i<4;i++) {string(v,"android.hardware.graphics.common.PlaneLayoutComponentType");u64(v,types[i]);u64(v,i*8);u64(v,8);}
        for(uint64_t n : {offset,UINT64_C(32),UINT64_C(256),UINT64_C(64),UINT64_C(64),UINT64_C(16384),UINT64_C(1),UINT64_C(1)})u64(v,n);
        break;
    }
    default:return -AIMAPPER_ERROR_UNSUPPORTED;
    }
    if (key!=6 && key!=7 && key!=15)u64(v,value);
    assert(capacity>=v.size());std::memcpy(data,v.data(),v.size());return static_cast<int32_t>(v.size());
}
static AIMapper_Error import_buffer(const native_handle_t *h, buffer_handle_t *out){*out=h;return AIMAPPER_ERROR_NONE;}
static AIMapper_Error free_buffer(buffer_handle_t){normal_frees++;return AIMAPPER_ERROR_NONE;}
static AIMapper_Error lock_buffer(buffer_handle_t,uint64_t,ARect,int fd,void **out){
    normal_locks++;if(fd>=0)fixture_close(fd);*out=lock_result==0?reinterpret_cast<void*>(0x1234):nullptr;
    return static_cast<AIMapper_Error>(lock_result);
}
static AIMapper_Error unlock_buffer(buffer_handle_t,int *fd){normal_unlocks++;*fd=-1;return AIMAPPER_ERROR_NONE;}
static AIMapper_Error load_platform(AIMapper **out){*out=&platform;return AIMAPPER_ERROR_NONE;}
static void *fixture_dlopen(const char *name,int){assert(!std::strcmp(name,"/vendor/lib64/hw/mapper.minigbm.so"));return &platform;}
static void *fixture_dlsym(void *h,const char *name){assert(h==&platform&&!std::strcmp(name,"AIMapper_loadIMapper"));return reinterpret_cast<void*>(load_platform);}
static int fixture_fcntl(int fd,int command,...){assert(command==F_DUPFD_CLOEXEC);assert(fd==10||fds.count(fd));if(dup_failure){errno=EMFILE;return -1;}int result=next_fd++;fds.insert(result);return result;}
static int fixture_close(int fd){assert(fd!=10&&fds.erase(fd)==1);return 0;}
static int fixture_fstat(int,struct stat *s){std::memset(s,0,sizeof(*s));s->st_dev=9;s->st_ino=10;s->st_size=16384;return 0;}
static int fixture_ioctl(int fd,unsigned long request,...){
    assert(fds.count(fd));va_list ap;va_start(ap,request);void *p=va_arg(ap,void*);va_end(ap);
    if(request==SYNC_IOC_FILE_INFO){if(fence_failure==2){errno=ENOTTY;return -1;}auto *info=static_cast<sync_file_info*>(p);info->status=fence_failure==1?-1:(fence_failure==3&&!polls?0:1);return 0;}
    assert(request==DMA_BUF_IOCTL_SYNC);auto flags=static_cast<dma_buf_sync*>(p)->flags;
    direction_seen=flags&DMA_BUF_SYNC_RW;
    if(flags&DMA_BUF_SYNC_END){ends++;if(ioctl_failure==2){errno=EIO;return -1;}}
    else {starts++;if(ioctl_failure==1){errno=EIO;return -1;}}
    return 0;
}
static int fixture_poll(pollfd *p,nfds_t n,int timeout){assert(n==1&&timeout==-1&&fds.count(p->fd));polls++;p->revents=fence_failure==4?POLLERR:POLLIN;return 1;}
static void *fixture_mmap(void *,size_t size,int protection,int flags,int fd,off_t at){
    assert(flags==MAP_SHARED&&fds.count(fd)&&at==0&&(protection&(PROT_READ|PROT_WRITE)));
    if(map_failure){errno=ENOMEM;return MAP_FAILED;}assert(!mapped);mapped=std::malloc(size);return mapped;
}
static int fixture_munmap(void *p,size_t){assert(p==mapped);unmaps++;std::free(mapped);mapped=nullptr;return 0;}
static void reset(){
    assert(!accesses&&fds.empty()&&!mapped);
    normal_locks=normal_unlocks=normal_frees=starts=ends=unmaps=polls=0;
    next_fd=100;lock_result=3;ioctl_failure=fence_failure=0;dup_failure=map_failure=metadata_failure=false;
    allocation_usage=0x100;modifier=0;layers=1;offset=0;format=1;direction_seen=0;
    platform={};platform.version=static_cast<AIMapper_Version>(5);platform.v5.importBuffer=import_buffer;
    platform.v5.freeBuffer=free_buffer;platform.v5.lock=lock_buffer;platform.v5.unlock=unlock_buffer;
    platform.v5.getStandardMetadata=metadata;real_mapper=&platform;
    auto *h=reinterpret_cast<native_handle_t*>(handle_storage);std::memset(h,0,sizeof(handle_storage));
    h->version=sizeof(native_handle_t);h->numFds=1;h->data[0]=10;handle=h;
}
static void rejected(const char *name,uint64_t flags=0x33,ARect rect={0,0,64,64}){
    void *data=reinterpret_cast<void*>(1);auto result=private_lock(handle,flags,rect,-1,&data);
    assert(result!=AIMAPPER_ERROR_NONE&&!data&&!accesses&&fds.empty()&&!mapped&&!starts);
    std::printf("PASS %s result=%d\n",name,result);
}
int main(){
    reset();allocation_usage=0x333;lock_result=0;fds.insert(30);void *data=nullptr;
    assert(private_lock(handle,0x33,{0,0,64,64},30,&data)==AIMAPPER_ERROR_NONE&&data==reinterpret_cast<void*>(0x1234));
    assert(fds.empty()&&!starts);int fence=123;assert(private_unlock(handle,&fence)==AIMAPPER_ERROR_NONE&&normal_unlocks==1&&fence==-1);puts("PASS normal CPU allocation forwards");
    reset();fds.insert(30);assert(private_lock(handle,0x33,{0,0,64,64},30,&data)==AIMAPPER_ERROR_NONE&&data==mapped);
    assert(starts==1&&polls==1&&fds.size()==1&&direction_seen==DMA_BUF_SYNC_RW);
    assert(private_free(handle)==AIMAPPER_ERROR_NO_RESOURCES);std::memset(data,0xa5,16384);
    assert(private_unlock(handle,&fence)==AIMAPPER_ERROR_NONE&&ends==1&&unmaps==1&&fence==-1);
    assert(fds.empty()&&!accesses);puts("PASS actual fallback pointer/fence/START/END/free pairing");
    reset();assert(private_lock(handle,0x20,{0,0,0,0},-1,&data)==AIMAPPER_ERROR_NONE&&direction_seen==DMA_BUF_SYNC_WRITE);
    assert(private_unlock(handle,&fence)==AIMAPPER_ERROR_NONE);puts("PASS write-only zero region");
    reset();rejected("zero CPU flags",0);
    reset();rejected("non-CPU flags",0x100);
    reset();rejected("invalid read enum",1);
    reset();rejected("rectangle outside width",0x33,{0,0,65,64});
    reset();rejected("negative rectangle",0x33,{-1,0,64,64});
    reset();allocation_usage=0x333;rejected("CPU allocation BAD_VALUE not swallowed");
    reset();allocation_usage=0;rejected("non-GPU allocation");
    reset();allocation_usage|=1ull<<14;rejected("protected buffer");
    reset();modifier=1;rejected("nonlinear modifier");
    reset();layers=2;rejected("multiple layers");
    reset();offset=4;rejected("plane exceeds real FD size");
    reset();format=35;rejected("YUV unsupported");
    reset();metadata_failure=true;rejected("metadata failure");
    reset();lock_result=5;rejected("real resource failure retained");
    reset();fds.insert(30);fence_failure=1;assert(private_lock(handle,0x33,{0,0,64,64},30,&data)==AIMAPPER_ERROR_BAD_VALUE);
    assert(fds.empty()&&!starts&&!mapped);puts("PASS failed acquire fence");
    reset();fds.insert(30);fence_failure=2;assert(private_lock(handle,0x33,{0,0,64,64},30,&data)==AIMAPPER_ERROR_BAD_VALUE);
    assert(fds.empty()&&!starts&&!mapped);puts("PASS non-sync_file acquire FD");
    reset();fds.insert(30);fence_failure=3;assert(private_lock(handle,0x33,{0,0,64,64},30,&data)==AIMAPPER_ERROR_NONE&&polls==1);
    assert(private_unlock(handle,&fence)==AIMAPPER_ERROR_NONE);puts("PASS pending fence waits before START");
    reset();fds.insert(30);fence_failure=4;assert(private_lock(handle,0x33,{0,0,64,64},30,&data)==AIMAPPER_ERROR_BAD_VALUE);
    assert(fds.empty()&&!starts&&!mapped);puts("PASS acquire fence POLLERR");
    reset();fds.insert(30);dup_failure=true;assert(private_lock(handle,0x33,{0,0,64,64},30,&data)==AIMAPPER_ERROR_NO_RESOURCES);
    assert(fds.empty()&&!normal_locks);puts("PASS fence duplicate failure consumes owned FD");
    reset();map_failure=true;assert(private_lock(handle,0x33,{0,0,64,64},-1,&data)==AIMAPPER_ERROR_NO_RESOURCES);
    assert(fds.empty()&&!accesses&&!starts);puts("PASS mmap failure");
    reset();ioctl_failure=1;assert(private_lock(handle,0x33,{0,0,64,64},-1,&data)==AIMAPPER_ERROR_NO_RESOURCES);
    assert(fds.empty()&&!accesses&&!mapped&&starts==1&&ends==0);puts("PASS START failure no success");
    reset();assert(private_lock(handle,0x33,{0,0,64,64},-1,&data)==AIMAPPER_ERROR_NONE);
    alignas(native_handle_t) unsigned char other_storage[sizeof(handle_storage)];std::memcpy(other_storage,handle_storage,sizeof(handle_storage));
    auto *other=reinterpret_cast<native_handle_t*>(other_storage);void *other_data=nullptr;
    assert(private_lock(other,0x33,{0,0,64,64},-1,&other_data)==AIMAPPER_ERROR_NO_RESOURCES&&!other_data&&starts==1&&fds.size()==1);
    assert(private_unlock(handle,&fence)==AIMAPPER_ERROR_NONE);puts("PASS distinct imports same DMA-BUF conflict");
    reset();assert(private_lock(handle,0x33,{0,0,64,64},-1,&data)==AIMAPPER_ERROR_NONE);ioctl_failure=2;
    assert(private_unlock(handle,&fence)==AIMAPPER_ERROR_NO_RESOURCES&&mapped&&accesses&&fds.size()==1&&unmaps==0);
    assert(private_free(handle)==AIMAPPER_ERROR_NO_RESOURCES);puts("PASS END failure quarantines mapping/import/FD");
    // Fixture cleanup invokes the actual retry; old mapper3's error path does
    // not retain its lookup slot, so this is NOT a product recovery claim.
    ioctl_failure=0;assert(private_unlock(handle,&fence)==AIMAPPER_ERROR_NONE);assert(!accesses&&fds.empty()&&!mapped);
    puts("CPU_ACCESS_FIXTURES_PASS cases=26 real_candidate_functions=1 GPU_tested=0");
}
