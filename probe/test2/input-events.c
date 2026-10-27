#include <linux/input.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#define BIT(a,n) (((a)[(n)/8] >> ((n)%8)) & 1)
static long seconds(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec; }
int main(int argc,char **argv) {
    const char *wanted=argc>1?argv[1]:"all";
    int duration=argc>2?atoi(argv[2]):30;
    int mt_mode=!strcmp(wanted,"multitouch");
    int slots[64]={0},contacts[64][64]={{0}},max_contacts[64]={0},dropped[64]={0};
    if(duration<1 || duration>180) return 2;
    struct pollfd p[64]; char names[64][256]; unsigned long counts[64]={0},absn[64]={0},syncn[64]={0}; int n=0;
    setvbuf(stdout,NULL,_IOLBF,0);
    for(int i=0;i<64;i++) {
        char path[64]; unsigned char absbits[(ABS_MAX+8)/8]={0},props[8]={0},keys[(KEY_MAX+8)/8]={0};
        snprintf(path,sizeof(path),"/dev/input/event%d",i); int fd=open(path,O_RDONLY|O_NONBLOCK);
        if(fd<0) continue;
        ioctl(fd,EVIOCGBIT(EV_ABS,sizeof(absbits)),absbits); ioctl(fd,EVIOCGPROP(sizeof(props)),props); ioctl(fd,EVIOCGBIT(EV_KEY,sizeof(keys)),keys);
        memset(names[n],0,sizeof(names[n]));
        ioctl(fd,EVIOCGNAME(sizeof(names[n])-1),names[n]);
        int xy=BIT(absbits,ABS_MT_POSITION_X)||BIT(absbits,ABS_X);
        /* The real SQ2 baseline Type Cover exposes PROP=0 and BTN_TOUCH,
         * but no BTN_TOOL_FINGER. Preserve it as a candidate by device name. */
        int pad=BIT(keys,BTN_TOOL_FINGER)||strstr(names[n],"Touchpad")||strstr(names[n],"touchpad")||strstr(names[n],"Trackpad");
        int direct=BIT(props,INPUT_PROP_DIRECT);
        int candidate=BIT(keys,BTN_TOUCH)&&!BIT(keys,BTN_TOOL_PEN);
        const char *kind=BIT(keys,BTN_TOOL_PEN)?"stylus":direct?"touchscreen":pad?"touchpad":"touchscreen";
        if(!xy || (!direct&&!pad&&!candidate) || (mt_mode ? (strcmp(kind,"touchscreen") || !BIT(absbits,ABS_MT_SLOT) || !BIT(absbits,ABS_MT_TRACKING_ID) || !BIT(absbits,ABS_MT_POSITION_X)) : (strcmp(wanted,"all")&&strcmp(wanted,kind)))) { close(fd); continue; }
        printf("CLASSIFICATION direct=%d finger_tool=%d name_fallback=%d touch_candidate=%d\n",direct,BIT(keys,BTN_TOOL_FINGER),!!pad,candidate);
        printf("DEVICE %s kind=%s name=%s\n",path,kind,names[n]);
        for(int a=0;a<=ABS_MAX;a++) if(BIT(absbits,a)) { struct input_absinfo info; if(ioctl(fd,EVIOCGABS(a),&info)==0) printf("ABS_CAP code=%d min=%d max=%d resolution=%d\n",a,info.minimum,info.maximum,info.resolution); }
        p[n]=(struct pollfd){.fd=fd,.events=POLLIN}; n++;
    }
    if(!n) { printf("NO_DEVICE kind=%s\n",wanted); return 2; }
    printf("RECORDING kind=%s seconds=%d. Tap, swipe, and use two fingers now.\n",wanted,duration);
    long end=seconds()+duration; unsigned long printed=0;
    while(seconds()<end) {
        int rc=poll(p,n,250); if(rc<0&&errno!=EINTR) break;
        for(int i=0;i<n;i++) if(p[i].revents&POLLIN) {
            struct input_event e[32]; ssize_t got;
            while((got=read(p[i].fd,e,sizeof(e)))>0) for(size_t k=0;k<(size_t)got/sizeof(*e);k++) {
                if(e[k].type==EV_SYN && e[k].code==SYN_DROPPED) dropped[i]=1;
                if(e[k].type==EV_ABS && e[k].code==ABS_MT_SLOT) slots[i]=e[k].value;
                if(e[k].type==EV_ABS && e[k].code==ABS_MT_TRACKING_ID && slots[i]>=0 && slots[i]<64)
                    contacts[i][slots[i]]=e[k].value>=0;
                if(e[k].type==EV_SYN && e[k].code==SYN_REPORT) {
                    int current=0; for(int s=0;s<64;s++)current+=contacts[i][s];
                    if(current>max_contacts[i])max_contacts[i]=current;
                }
                if(e[k].type==EV_ABS || e[k].type==EV_SYN || (e[k].type==EV_KEY&&e[k].code>=BTN_MOUSE)) {
                    counts[i]++; if(e[k].type==EV_ABS)absn[i]++; if(e[k].type==EV_SYN)syncn[i]++;
                    if(printed++<10000)printf("EVENT device=%d t=%ld.%06ld type=%u code=%u value=%d\n",i,(long)e[k].input_event_sec,(long)e[k].input_event_usec,e[k].type,e[k].code,e[k].value);
                }
            }
        }
    }
    int active=0,mt_ok=0;
    for(int i=0;i<n;i++) {
        printf("MULTITOUCH name=%s max_simultaneous_contacts=%d dropped=%d\n",names[i],max_contacts[i],dropped[i]);
        if(max_contacts[i]>=2 && !dropped[i] && absn[i])mt_ok++;
    }
    for(int i=0;i<n;i++) { printf("SUMMARY name=%s events=%lu absolute=%lu sync=%lu\n",names[i],counts[i],absn[i],syncn[i]); if(absn[i])active++; close(p[i].fd); }
    printf("RESULT devices=%d active_absolute_devices=%d (gestures require desktop userspace; this records raw events)\n",n,active);
    if(mt_mode) { printf("RESULT multitouch_devices_with_two_contacts=%d\n",mt_ok); return mt_ok?0:3; }
    return active?0:3;
}
