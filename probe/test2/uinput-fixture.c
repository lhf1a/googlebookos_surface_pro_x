/* QEMU-only test fixture: creates a virtual screen and emits 1 or 2 contacts. */
#include <linux/uinput.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
static void event(int fd,int type,int code,int value) {
    struct input_event e={.type=type,.code=code,.value=value};
    if(write(fd,&e,sizeof(e))!=sizeof(e)) { perror("write");exit(1); }
}
static void axis(int fd,int code,int max) {
    struct uinput_abs_setup a={.code=code,.absinfo={.minimum=0,.maximum=max}};
    if(ioctl(fd,UI_SET_ABSBIT,code)<0 || ioctl(fd,UI_ABS_SETUP,&a)<0) {perror("axis");exit(1);}
}
int main(int argc,char **argv) {
    int count=argc>1?atoi(argv[1]):2;
    if(count<1 || count>2)return 2;
    int fd=open("/dev/uinput",O_WRONLY|O_NONBLOCK);
    if(fd<0){perror("uinput");return 1;}
    if(ioctl(fd,UI_SET_EVBIT,EV_ABS)<0 || ioctl(fd,UI_SET_PROPBIT,INPUT_PROP_DIRECT)<0)return 1;
    axis(fd,ABS_MT_SLOT,9);axis(fd,ABS_MT_TRACKING_ID,65535);
    axis(fd,ABS_MT_POSITION_X,32767);axis(fd,ABS_MT_POSITION_Y,32767);
    struct uinput_setup setup={.id={.bustype=BUS_VIRTUAL,.vendor=1,.product=1}};
    strcpy(setup.name,"SQ2 QEMU synthetic touchscreen");
    if(ioctl(fd,UI_DEV_SETUP,&setup)<0 || ioctl(fd,UI_DEV_CREATE)<0){perror("create");return 1;}
    sleep(3);
    for(int t=0;t<20;t++) {
        for(int s=0;s<count;s++) {
            event(fd,EV_ABS,ABS_MT_SLOT,s);
            if(t==0)event(fd,EV_ABS,ABS_MT_TRACKING_ID,s+1);
            event(fd,EV_ABS,ABS_MT_POSITION_X,1000+s*100+t*10);
            event(fd,EV_ABS,ABS_MT_POSITION_Y,2000+t*10);
        }
        event(fd,EV_SYN,SYN_REPORT,0);usleep(100000);
    }
    for(int s=0;s<count;s++){event(fd,EV_ABS,ABS_MT_SLOT,s);event(fd,EV_ABS,ABS_MT_TRACKING_ID,-1);}
    event(fd,EV_SYN,SYN_REPORT,0);sleep(3);
    ioctl(fd,UI_DEV_DESTROY);close(fd);return 0;
}
