/* Finite, independent kernel-log evidence for global graphics trials.
 * No GPU commands or system configuration writes. Not a boot service.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static volatile sig_atomic_t stopping;
static void stop(int signal){(void)signal;stopping=1;}
static int64_t milliseconds(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static int write_all(int fd,const void *data,size_t size){
    const char *p=data;
    while(size){ssize_t n=write(fd,p,size);if(n<0&&errno==EINTR)continue;if(n<=0)return -1;p+=n;size-=n;}
    return 0;
}
int main(int argc,char **argv){
    if(argc!=4)return 2;
    char *end;unsigned long seconds=strtoul(argv[2],&end,10);if(*end||seconds<1||seconds>135)return 2;
    unsigned long limit=strtoul(argv[3],&end,10);if(*end||limit<4096||limit>2097152)return 2;
    int input=open("/dev/kmsg",O_RDONLY|O_NONBLOCK|O_CLOEXEC);if(input<0){perror("open kmsg");return 1;}
    if(lseek(input,0,SEEK_END)<0){perror("seek kmsg");close(input);return 1;}
    int output=open(argv[1],O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_DSYNC,0600);
    if(output<0){perror("create evidence");close(input);return 1;}
    int64_t started=milliseconds();if(started<0)return 1;
    char header[160];int n=snprintf(header,sizeof(header),"KMSG_READY pid=%ld seconds=%lu limit=%lu start_ms=%lld\n",(long)getpid(),seconds,limit,(long long)started);
    if(write_all(output,header,(size_t)n))return 1;
    struct sigaction action={0};action.sa_handler=stop;sigemptyset(&action.sa_mask);
    sigaction(SIGTERM,&action,NULL);sigaction(SIGINT,&action,NULL);sigaction(SIGHUP,&action,NULL);
    size_t total=(size_t)n,records=0,filtered=0;const char *reason="timeout";int failed=0;
    while(!stopping){
        int64_t now=milliseconds();if(now<0){reason="clock-error";failed=1;break;}
        int64_t remain=started+(int64_t)seconds*1000-now;if(remain<=0)break;
        struct pollfd pending={input,POLLIN,0};
        int ready=poll(&pending,1,remain>1000?1000:(int)remain);
        if(ready<0&&errno==EINTR)continue;
        if(ready<0||(ready&&(pending.revents&(POLLNVAL|POLLHUP)))){reason="poll-error";failed=1;break;}
        if(!ready)continue;
        char record[65537];ssize_t count=read(input,record,sizeof(record)-1);
        if(count<0&&(errno==EAGAIN||errno==EINTR))continue;
        if(count<0&&errno==EPIPE){const char missed[]="KMSG_OVERRUN\n";if(total+sizeof(missed)-1+192>limit){reason="limit";break;}
            if(write_all(output,missed,sizeof(missed)-1)){failed=1;reason="write-error";break;}total+=sizeof(missed)-1;continue;}
        if(count<=0){reason="read-error";failed=1;break;}
        record[count]=0;
        unsigned long priority=strtoul(record,NULL,10);
        // init/SELinux userspace chatter otherwise consumes a bounded capture
        // before a graphics fault. Keep all kernel-facility records plus the
        // relevant service/renderer transitions and our explicit smoke marker.
        if(priority>=8&&!strstr(record,"surfaceflinger")&&!strstr(record,"RenderEngine")&&
           !strstr(record,";SQ2_KMSG_SMOKE")){filtered++;continue;}
        if(total+(size_t)count+192>limit){reason="limit";break;}
        if(write_all(output,record,(size_t)count)){reason="write-error";failed=1;break;}
        total+=(size_t)count;records++;
    }
    if(stopping)reason="signal";
    n=snprintf(header,sizeof(header),"KMSG_END reason=%s records=%zu filtered=%zu bytes=%zu elapsed_ms=%lld\n",reason,records,filtered,total,(long long)(milliseconds()-started));
    if(write_all(output,header,(size_t)n)||fsync(output))failed=1;
    close(output);close(input);return failed?1:0;
}
