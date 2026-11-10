#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/blkpg.h>
#include <linux/fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
static unsigned long long number(const char *s) {
    char *end; errno=0; unsigned long long n=strtoull(s,&end,10);
    if (errno || !*s || *end || *s=='-' || n>LLONG_MAX) exit(2);
    return n;
}
int main(int argc,char **argv) {
    if(argc!=5) return 2;
    unsigned long long n=number(argv[2]),start=number(argv[3]),length=number(argv[4]),bytes=0;
    if(!n || n>INT_MAX || !length || start>LLONG_MAX-length) return 2;
    int fd=open(argv[1],O_RDONLY|O_CLOEXEC); if(fd<0){perror("disk");return 1;}
    if(ioctl(fd,BLKGETSIZE64,&bytes) || start+length>bytes){perror("disk size");close(fd);return 1;}
    struct blkpg_partition p={.start=start,.length=length,.pno=n};
    struct blkpg_ioctl_arg a={.op=BLKPG_RESIZE_PARTITION,.datalen=sizeof(p),.data=&p};
    int rc=ioctl(fd,BLKPG,&a);if(rc)perror("BLKPG_RESIZE_PARTITION");close(fd);return rc?1:0;
}
