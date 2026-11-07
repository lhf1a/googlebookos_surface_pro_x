#define _GNU_SOURCE
#define BIONIC_IOCTL_NO_SIGNEDNESS_OVERLOAD
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>
static void opened(const char *label,int fd,int saved){printf("%s result=%d errno=%d\n",label,fd>=0?0:-1,saved);if(fd>=0)close(fd);}
int main(void){
 int fd; errno=77;fd=open("/dev/null",O_RDONLY);opened("open",fd,errno);
 errno=77;fd=open64("/dev/null",O_RDONLY);opened("open64",fd,errno);
 errno=77;fd=openat(AT_FDCWD,"/dev/null",O_RDONLY);opened("openat",fd,errno);
 int(*fortify)(const char*,int)=dlsym(RTLD_DEFAULT,"__open_2");
 if(!fortify)return 1;
 errno=77;fd=fortify("/dev/null",O_RDONLY);opened("__open_2",fd,errno);
 errno=77;fd=open("/dev/ion",O_RDWR);opened("ion",fd,errno);
 errno=77;fd=open("/dev/video32",O_RDWR);opened("video32",fd,errno);
 int p[2];if(pipe(p))return 2;if(write(p[1],"abc",3)!=3)return 3;
 int count=0;errno=77;int r=ioctl(p[0],FIONREAD,&count);int e=errno;
 printf("ioctl-read result=%d errno=%d count=%d\n",r,e,count);
 errno=77;r=ioctl(p[0],FIOCLEX);e=errno;int clo=fcntl(p[0],F_GETFD);
 printf("ioctl-noarg result=%d errno=%d cloexec=%d\n",r,e,(clo&FD_CLOEXEC)!=0);
 errno=77;r=ioctl(p[0],0x4020565a,&count);e=errno;
 printf("ioctl-error result=%d errno=%d\n",r,e);
 close(p[0]);close(p[1]);return 0;
}