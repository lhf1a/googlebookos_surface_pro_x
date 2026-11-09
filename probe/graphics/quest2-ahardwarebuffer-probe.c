/* Standard libandroid/libui allocation through the selected real HAL. */
#include <android/hardware_buffer.h>
#include <assert.h>
#include <errno.h>
#ifdef QUEST2_NATIVE_MAPPER5
#include <dlfcn.h>
#endif
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static void unlock(AHardwareBuffer *buffer) {
    int fd=-1;
    assert(AHardwareBuffer_unlock(buffer,&fd)==0);
    if(fd>=0) {
        struct pollfd p={.fd=fd,.events=POLLIN};int r;
        do {r=poll(&p,1,5000);}while(r<0 && errno==EINTR);
        assert(r==1 && (p.revents&POLLIN) && !(p.revents&(POLLERR|POLLNVAL)));
        close(fd);
    }
}
static void check_libraries(void) {
    FILE *f=fopen("/proc/self/maps","r");assert(f);
    char line[2048];int hal=0,original=0,ui=0;
    while(fgets(line,sizeof(line),f)) {
        assert(!strstr(line,"libquest2-native-mapper.so"));
        assert(!strstr(line,"mapper.minigbm.so"));
#ifdef QUEST2_NATIVE_MAPPER5
        assert(!strstr(line,"libquest2-original-mapper3.so") && !strstr(line,"-impl-quest2.so"));
        if(strstr(line,"mapper.quest2.so"))hal=1;
        if(strstr(line,"gralloc.kona.so"))original=1;
#else
        if(strstr(line,"android.hardware.graphics.mapper@3.0-impl-quest2.so"))hal=1;
        if(strstr(line,"libquest2-original-mapper3.so"))original=1;
#endif
        if(strstr(line,"/libui.so"))ui=1;
    }
    fclose(f);assert(hal && original && ui);
#ifdef QUEST2_NATIVE_MAPPER5
    puts("AHB_LIBUI_HAL mapper5=1 libui=1 private_getService=0");
#else
    puts("AHB_LIBUI_HAL mapper3=1 libui=1 private_getService=0");
#endif
}
static int receive_buffers(int socket,int control) {
    for(unsigned i=0;i<27;i++) {
        AHardwareBuffer *buffer=NULL;
        assert(AHardwareBuffer_recvHandleFromUnixSocket(socket,&buffer)==0 && buffer);
        char go=0;assert(read(control,&go,1)==1 && go=='G');
        AHardwareBuffer_Desc d={0};AHardwareBuffer_describe(buffer,&d);
        const uint32_t formats[]={1,43,22},widths[]={64,129,1280};
        assert(d.format==formats[i/9] && d.width==widths[(i/3)%3] &&
               d.height==(d.width==1280?720:65) && d.layers==1);
        void *data=NULL;int32_t pixel=-1,row=-1;
        assert(AHardwareBuffer_lockAndGetInfo(buffer,AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,
            -1,NULL,&data,&pixel,&row)==0 && data);
        uint32_t bpp=d.format==22?8:4;
        assert(pixel==(int32_t)bpp && row==(int32_t)(d.stride*bpp));
        for(uint32_t y=0;y<d.height;y++)for(uint32_t x=0;x<d.width*bpp;x++)
            assert(*((unsigned char *)data+(size_t)y*row+x)==(unsigned char)(x*17+y*29+(i%3)*43));
        unlock(buffer);AHardwareBuffer_release(buffer);
        assert(write(control,"A",1)==1);
    }
    check_libraries();
    puts("AHB_EXEC_IMPORT_PASS buffers=27 producer_released=1 pixel_exact=1");
    return 0;
}
int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IONBF,0);
#ifdef QUEST2_NATIVE_MAPPER5
    if(argc==2 && !strcmp(argv[1],"--loader")) {
        void *support=dlopen("libvndksupport.so",RTLD_NOW|RTLD_LOCAL);
        if(!support){printf("SPHAL_SUPPORT_ERROR %s\n",dlerror());return 1;}
        void *(*load)(const char *,int)=dlsym(support,"android_load_sphal_library");
        assert(load);
        void *candidate=load("mapper.quest2.so",RTLD_NOW|RTLD_LOCAL);
        printf("SPHAL_MAPPER_LOAD loaded=%d error=%s\n",candidate!=NULL,candidate?"none":dlerror());
        return candidate?0:1;
    }
#endif
    if(argc==4 && !strcmp(argv[1],"--receive"))return receive_buffers(atoi(argv[2]),atoi(argv[3]));
    assert(argc==1);
    int sockets[2],control[2];
    assert(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0 &&
           socketpair(AF_UNIX,SOCK_STREAM,0,control)==0);
    pid_t child=fork();assert(child>=0);
    if(child==0) {
        close(sockets[0]);close(control[0]);
        char fd[32],sync[32];snprintf(fd,sizeof(fd),"%d",sockets[1]);snprintf(sync,sizeof(sync),"%d",control[1]);
        execl("/proc/self/exe",argv[0],"--receive",fd,sync,NULL);_exit(127);
    }
    close(sockets[1]);close(control[1]);
    const uint32_t formats[]={1,43,22},widths[]={64,129,1280};
    unsigned count=0;
    for(unsigned fi=0;fi<3;fi++)for(unsigned wi=0;wi<3;wi++) {
        const uint32_t w=widths[wi],h=w==1280?720:65,bpp=formats[fi]==22?8:4;
        AHardwareBuffer_Desc desc={.width=w,.height=h,.layers=1,.format=formats[fi],
            .usage=AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN|AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN|
                   AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE};
        AHardwareBuffer *buffers[3]={0};
        for(unsigned i=0;i<3;i++) {
            int r=AHardwareBuffer_allocate(&desc,&buffers[i]);
            printf("AHB_ALLOCATE format=%u width=%u height=%u error=%d\n",desc.format,w,h,r);
            assert(r==0 && buffers[i]);
        }
        check_libraries();
        for(unsigned i=0;i<3;i++) {
            AHardwareBuffer_Desc actual={0};AHardwareBuffer_describe(buffers[i],&actual);
            assert(actual.width==w && actual.height==h && actual.format==desc.format &&
                   actual.layers==1 && actual.stride>=w && actual.usage==desc.usage);
            for(unsigned pass=0;pass<2;pass++) {
                void *data=NULL;int32_t pixel=-1,row=-1;
                int r=AHardwareBuffer_lockAndGetInfo(buffers[i],pass?AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN:
                    AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,-1,NULL,&data,&pixel,&row);
                printf("AHB_LOCK error=%d bpp=%d row=%d stride=%u\n",r,pixel,row,actual.stride);
                assert(r==0 && data && pixel==(int32_t)bpp && row==(int32_t)(actual.stride*bpp));
                for(uint32_t y=0;y<h;y++)for(uint32_t x=0;x<w*bpp;x++) {
                    unsigned char *v=(unsigned char *)data+(size_t)y*row+x;
                    unsigned char expected=(unsigned char)(x*17+y*29+i*43);
                    if(pass)assert(*v==expected);else *v=expected;
                }
                unlock(buffers[i]);
            }
            assert(AHardwareBuffer_sendHandleToUnixSocket(buffers[i],sockets[0])==0);
            AHardwareBuffer_release(buffers[i]);count++;
            assert(write(control[0],"G",1)==1);
            char ack=0;assert(read(control[0],&ack,1)==1 && ack=='A');
        }
    }
    close(sockets[0]);close(control[0]);
    int status=0;assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
    printf("AHB_LIBUI_PASS buffers=%u pixel_exact=1\n",count);return 0;
}
