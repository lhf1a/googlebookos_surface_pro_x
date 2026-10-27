/* Open both endpoints before SELinux/init handoff. Persist every kernel record. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
static int fullwrite(int fd, const char *b, size_t n) {
    while (n) { ssize_t r=write(fd,b,n); if(r<0) { if(errno==EINTR)continue; return -1; } b+=r;n-=r; } return 0;
}
int main(int argc, char **argv) {
    if(argc!=2)return 2;
    int k=open("/dev/kmsg",O_RDONLY), f=open(argv[1],O_WRONLY|O_CREAT|O_APPEND|O_DSYNC,0600);
    if(k<0||f<0){perror("boot-kmsg open");return 3;}
    pid_t p=fork(); if(p<0)return 4; if(p>0)return 0;
    setsid(); signal(SIGHUP,SIG_IGN); signal(SIGTERM,SIG_IGN);
    char b[16384];
    for(;;){ssize_t n=read(k,b,sizeof b);if(n>0){if(fullwrite(f,b,n))return 5;}else if(n<0&&errno!=EINTR&&errno!=EPIPE){perror("boot-kmsg read");return 6;}}
}
