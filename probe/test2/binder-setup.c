#include <linux/android/binderfs.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int main(void) {
    const char *names[]={"binder","hwbinder","vndbinder"};
    int fd=open("/dev/binderfs/binder-control",O_RDONLY);
    if(fd<0){perror("binder-control");return 1;}
    for(int i=0;i<3;i++) {
        char path[128];snprintf(path,sizeof(path),"/dev/binderfs/%s",names[i]);
        if(access(path,F_OK)!=0) {
            struct binderfs_device d={0};strcpy(d.name,names[i]);
            if(ioctl(fd,BINDER_CTL_ADD,&d)<0){perror("BINDER_CTL_ADD");return 1;}
        }
        printf("BINDER_DEVICE_READY=%s\n",names[i]);
    }
    close(fd);return 0;
}
