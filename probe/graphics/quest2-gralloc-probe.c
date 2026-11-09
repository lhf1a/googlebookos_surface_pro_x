/* SPDX-License-Identifier: MIT
 * Exercise the actual donor gralloc1 allocator without service registration.
 * Native descriptor/handle, CPU cache operations and retained lifetime.
 * Optional QUEST2_NATIVE_GPU adds original mapper/EGL RGBA8/RGB10A2/FP16 tests.
 * Neither mode proves VPU, scanout, UBWC or protected-buffer compatibility.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <hardware/gralloc1.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <string.h>
#include <unistd.h>
static unsigned pixel_bytes(int format) {
    assert(format==HAL_PIXEL_FORMAT_RGBA_8888 || format==HAL_PIXEL_FORMAT_RGBA_1010102 || format==HAL_PIXEL_FORMAT_RGBA_FP16);
    return format==HAL_PIXEL_FORMAT_RGBA_FP16?8:4;
}
static void pattern_pixel(int format,unsigned x,unsigned y,unsigned cycle,uint8_t *pixel) {
    if(format==HAL_PIXEL_FORMAT_RGBA_8888) {
        for(unsigned c=0;c<4;c++)pixel[c]=(uint8_t)(x*17+y*31+c*53+cycle);
    } else if(format==HAL_PIXEL_FORMAT_RGBA_1010102) {
        uint32_t word=((x*17+y*31+cycle)&1023)|(((x*37+y*13+cycle*3)&1023)<<10)|
            (((x*7+y*59+cycle*11)&1023)<<20)|(((x+y+cycle)&3)<<30);
        memcpy(pixel,&word,4);
    } else {
        const uint16_t values[]={0,0x3400,0x3800,0x3a00,0x3c00};
        uint16_t words[4];for(unsigned c=0;c<4;c++)words[c]=values[(x*17+y*31+c*53+cycle)%5];
        memcpy(pixel,words,8);
    }
}
static unsigned fd_count(void) {
    DIR *d=opendir("/proc/self/fd");assert(d);unsigned count=0;
    struct dirent *e;while((e=readdir(d)))if(e->d_name[0]!='.')count++;
    closedir(d);return count;
}
static void wait_fence(int fd) {
    if(fd<0)return;
    struct pollfd p={.fd=fd,.events=POLLIN};int r;
    do {r=poll(&p,1,5000);}while(r<0 && errno==EINTR);
    assert(r==1 && (p.revents&POLLIN) && !(p.revents&(POLLERR|POLLNVAL)));
    assert(!close(fd));
}
#define LOAD(name) GRALLOC1_PFN_##name name=(GRALLOC1_PFN_##name)dev->getFunction(dev,GRALLOC1_FUNCTION_##name);assert(name)
#ifdef QUEST2_NATIVE_GPU
#include "quest2-native-egl.h"
#endif
int main(int argc,char **argv) {
    if((argc!=2 && argc!=4) || argv[1][0]!='/')return 64;
    if(argc==4 && strcmp(argv[2],"--import"))return 64;
    void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!lib){fprintf(stderr,"GRALLOC_LOAD_FAIL %s\n",dlerror());return 1;}
    setvbuf(stdout,NULL,_IONBF,0);
    void *layout=dlopen("libadreno_utils.so",RTLD_NOW|RTLD_LOCAL);
    if(!layout){fprintf(stderr,"LAYOUT_LOAD_FAIL %s\n",dlerror());return 1;}
    const char *symbols[]={"gsl_device_getinfo", "adreno_init_memory_layout", "ion_alloc_fd"};
    for(unsigned i=0;i<3;i++) {
        void *symbol=dlsym(layout,symbols[i]);Dl_info origin={0};
        assert(symbol && dladdr(symbol,&origin) && origin.dli_fname);
        printf("GRALLOC_BINDING symbol=%s library=%s\n",symbols[i],origin.dli_fname);
    }
    hw_module_t *module=dlsym(lib,HAL_MODULE_INFO_SYM_AS_STR);
    assert(module && module->tag==HARDWARE_MODULE_TAG);
    gralloc1_device_t *dev=NULL;
    int err=gralloc1_open(module,&dev);
    printf("GRALLOC_OPEN result=%d module_version=%u\n",err,module->module_api_version);fflush(stdout);
    assert(!err && dev);
    LOAD(CREATE_DESCRIPTOR);LOAD(DESTROY_DESCRIPTOR);LOAD(SET_DIMENSIONS);
    LOAD(SET_FORMAT);LOAD(SET_LAYER_COUNT);LOAD(SET_PRODUCER_USAGE);LOAD(SET_CONSUMER_USAGE);
    LOAD(ALLOCATE);LOAD(RETAIN);LOAD(RELEASE);LOAD(GET_STRIDE);
    LOAD(GET_FORMAT);LOAD(GET_DIMENSIONS);LOAD(LOCK);LOAD(UNLOCK);
    uint32_t capability_count=0;dev->getCapabilities(dev,&capability_count,NULL);
    assert(capability_count<=32);int32_t capabilities[32]={0};
    dev->getCapabilities(dev,&capability_count,capabilities);
    int release_deletes=0;for(uint32_t i=0;i<capability_count;i++)
        if(capabilities[i]==GRALLOC1_CAPABILITY_RELEASE_IMPLY_DELETE)release_deletes=1;
    struct packet {uint32_t width,height,cycle;int format,version,fds,ints;int data[128];};
    if(argc==4) {
        char *end=NULL;long value=strtol(argv[3],&end,10);assert(end && !*end && value>=0 && value<=INT32_MAX);
        int sock=(int)value;struct packet packet={0};
        union {struct cmsghdr align;char bytes[CMSG_SPACE(16*sizeof(int))];} control={0};
        struct iovec iov={&packet,sizeof(packet)};
        struct msghdr message={.msg_iov=&iov,.msg_iovlen=1,.msg_control=control.bytes,.msg_controllen=sizeof(control)};
        assert(recvmsg(sock,&message,MSG_CMSG_CLOEXEC)==sizeof(packet));
        assert(!(message.msg_flags&(MSG_TRUNC|MSG_CTRUNC)));
        assert(packet.version==sizeof(native_handle_t) && packet.fds>0 && packet.fds<=16 && packet.ints>=0 && packet.ints<=128-packet.fds);
        assert(packet.width>0 && packet.width<=2880 && packet.height>0 && packet.height<=64);
        struct cmsghdr *c=CMSG_FIRSTHDR(&message);
        assert(c && c->cmsg_level==SOL_SOCKET && c->cmsg_type==SCM_RIGHTS && c->cmsg_len==CMSG_LEN(packet.fds*sizeof(int)) && !CMSG_NXTHDR(&message,c));
        native_handle_t *raw=calloc(1,sizeof(*raw)+(packet.fds+packet.ints)*sizeof(int));assert(raw);
        raw->version=packet.version;raw->numFds=packet.fds;raw->numInts=packet.ints;
        memcpy(raw->data,packet.data,(packet.fds+packet.ints)*sizeof(int));
        memcpy(raw->data,CMSG_DATA(c),packet.fds*sizeof(int));
        buffer_handle_t imported=raw;assert(!RETAIN(dev,imported));assert(!close(sock));
        uint32_t stride=0;assert(!GET_STRIDE(dev,imported,&stride) && stride>=packet.width);
        gralloc1_rect_t rect={0,0,(int32_t)packet.width,(int32_t)packet.height};void *address=NULL;
        assert(!LOCK(dev,imported,0,GRALLOC1_CONSUMER_USAGE_CPU_READ_OFTEN,&rect,&address,-1) && address);
        const uint8_t *bytes=address;unsigned bpp=pixel_bytes(packet.format);
        for(uint32_t y=0;y<packet.height;y++)for(uint32_t x=0;x<packet.width;x++) {
            uint8_t expected[8];pattern_pixel(packet.format,x,y,packet.cycle,expected);
            assert(!memcmp(bytes+((size_t)y*stride+x)*bpp,expected,bpp));
        }
        int fence=-1;assert(!UNLOCK(dev,imported,&fence));wait_fence(fence);
        assert(!RELEASE(dev,imported));
        if(!release_deletes){
            for(int i=0;i<raw->numFds;i++)if(raw->data[i]>=0)assert(!close(raw->data[i]));
            free(raw);
        }
        assert(!gralloc1_close(dev));
        puts("GRALLOC_EXEC_IMPORT_PASS scm_rights=1 independent_process=1 pixels=1");
        dlclose(layout);dlclose(lib);return 0;
    }
#ifdef QUEST2_NATIVE_GPU
    native_gpu_init(native_rgb_fragment);
#endif
    unsigned baseline=0;
    const uint32_t sizes[][2]={{64,64},{13,7},{2880,54}};
#ifdef QUEST2_NATIVE_GPU
    const int formats[]={HAL_PIXEL_FORMAT_RGBA_8888,HAL_PIXEL_FORMAT_RGBA_1010102,HAL_PIXEL_FORMAT_RGBA_FP16};
#else
    const int formats[]={HAL_PIXEL_FORMAT_RGBA_8888};
#endif
    unsigned cycles=9*sizeof(formats)/sizeof(formats[0]);
    for(unsigned cycle=0;cycle<cycles;cycle++) {
        int requested_format=formats[cycle/9];unsigned bpp=pixel_bytes(requested_format);
        uint32_t w=sizes[cycle%3][0],h=sizes[cycle%3][1];
        gralloc1_buffer_descriptor_t descriptor=0;
        assert(!CREATE_DESCRIPTOR(dev,&descriptor));
        assert(!SET_DIMENSIONS(dev,descriptor,w,h));
        assert(!SET_FORMAT(dev,descriptor,requested_format));
        assert(!SET_LAYER_COUNT(dev,descriptor,1));
        uint64_t producer=GRALLOC1_PRODUCER_USAGE_CPU_WRITE_OFTEN;
#ifdef QUEST2_NATIVE_GPU
        producer|=GRALLOC1_PRODUCER_USAGE_GPU_RENDER_TARGET;
#endif
        uint64_t consumer=GRALLOC1_CONSUMER_USAGE_CPU_READ_OFTEN|GRALLOC1_CONSUMER_USAGE_GPU_TEXTURE;
        assert(!SET_PRODUCER_USAGE(dev,descriptor,producer));
        assert(!SET_CONSUMER_USAGE(dev,descriptor,consumer));
        buffer_handle_t handle=NULL;
        err=ALLOCATE(dev,1,&descriptor,&handle);
        assert(!DESTROY_DESCRIPTOR(dev,descriptor));
        printf("GRALLOC_ALLOC cycle=%u width=%u height=%u result=%d\n",cycle,w,h,err);fflush(stdout);
        assert(!err && handle);
        assert(handle->version==sizeof(native_handle_t) && handle->numFds>0 && handle->numFds<=16);
        printf("GRALLOC_HANDLE fds=%d ints=%d\n",handle->numFds,handle->numInts);
        for(int i=0;i<handle->numFds;i++){struct stat st;assert(!fstat(handle->data[i],&st));printf("GRALLOC_FD index=%d bytes=%lld\n",i,(long long)st.st_size);}
        uint32_t stride=0,rw=0,rh=0;int32_t format=0;
        assert(!GET_STRIDE(dev,handle,&stride) && stride>=w);
        assert(!GET_FORMAT(dev,handle,&format) && format==requested_format);
        assert(!GET_DIMENSIONS(dev,handle,&rw,&rh) && rw==w && rh==h);
        gralloc1_rect_t rect={0,0,(int32_t)w,(int32_t)h};void *address=NULL;
        assert(!LOCK(dev,handle,producer,GRALLOC1_CONSUMER_USAGE_CPU_READ_OFTEN,&rect,&address,-1) && address);
        uint8_t *bytes=address;
        for(uint32_t y=0;y<h;y++)for(uint32_t x=0;x<w;x++)
            pattern_pixel(format,x,y,cycle,bytes+((size_t)y*stride+x)*bpp);
        int fence=-1;assert(!UNLOCK(dev,handle,&fence));wait_fence(fence);
        assert(!RETAIN(dev,handle));assert(!RELEASE(dev,handle));
        address=NULL;
        assert(!LOCK(dev,handle,0,GRALLOC1_CONSUMER_USAGE_CPU_READ_OFTEN,&rect,&address,-1) && address);
        bytes=address;
        for(uint32_t y=0;y<h;y++)for(uint32_t x=0;x<w;x++) {
            uint8_t expected[8];pattern_pixel(format,x,y,cycle,expected);
            assert(!memcmp(bytes+((size_t)y*stride+x)*bpp,expected,bpp));
        }
        fence=-1;assert(!UNLOCK(dev,handle,&fence));wait_fence(fence);
#ifdef QUEST2_NATIVE_GPU
        native_gpu_check(dev,handle,w,h,stride,cycle,format);
#endif
        int sockets[2];assert(!socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,sockets));
        struct packet packet={.width=w,.height=h,.cycle=cycle,.format=format,.version=handle->version,.fds=handle->numFds,.ints=handle->numInts};
        assert(packet.ints>=0 && packet.ints<=128-packet.fds);
        memcpy(packet.data,handle->data,(packet.fds+packet.ints)*sizeof(int));
        pid_t child=fork();assert(child>=0);
        if(!child){
            close(sockets[0]);assert(!fcntl(sockets[1],F_SETFD,0));
            char number[32];snprintf(number,sizeof(number),"%d",sockets[1]);
            execl(argv[0],argv[0],argv[1],"--import",number,(char *)NULL);_exit(127);
        }
        assert(!close(sockets[1]));
        union {struct cmsghdr align;char bytes[CMSG_SPACE(16*sizeof(int))];} control={0};
        struct iovec iov={&packet,sizeof(packet)};
        struct msghdr message={.msg_iov=&iov,.msg_iovlen=1,.msg_control=control.bytes,.msg_controllen=CMSG_SPACE(packet.fds*sizeof(int))};
        struct cmsghdr *c=CMSG_FIRSTHDR(&message);c->cmsg_level=SOL_SOCKET;c->cmsg_type=SCM_RIGHTS;c->cmsg_len=CMSG_LEN(packet.fds*sizeof(int));
        memcpy(CMSG_DATA(c),handle->data,packet.fds*sizeof(int));
        assert(sendmsg(sockets[0],&message,MSG_NOSIGNAL)==sizeof(packet));
        assert(!RELEASE(dev,handle));assert(!close(sockets[0]));
        int status=0;assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
        unsigned fds=fd_count();if(!cycle)baseline=fds;else assert(fds==baseline);
        printf("GRALLOC_PIXEL_PASS cycle=%u format=%d stride=%u fds=%u\n",cycle,format,stride,fds);
    }
#ifdef QUEST2_NATIVE_GPU
    native_gpu_finish();
#endif
    assert(!gralloc1_close(dev));
    dlclose(layout);dlclose(lib);
    printf("GRALLOC_MEMORY_PASS cycles=%u native_handle=1 cpu_roundtrip=1 retained_lifetime=1 exec_import=1 fd_stable=1\n",cycles);
    return 0;
}
