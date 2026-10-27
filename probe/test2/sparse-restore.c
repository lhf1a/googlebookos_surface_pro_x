/* Restore a trusted build-time sparse template into one new RAM file only. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/vfs.h>
#include <sys/stat.h>
#include <errno.h>
static void rd(void *p,size_t n) { while(n){ssize_t k=read(0,p,n);if(k<=0){perror("sparse input");_exit(3);}p=(char*)p+k;n-=k;} }
int main(int argc,char **argv) {
    if(argc!=2 || (strcmp(argv[1],"/run/googlebook-userdata.img") && strcmp(argv[1],"/run/googlebook-bootmeta.img")))return 2;
    struct statfs fs; if(statfs("/run",&fs)||fs.f_type!=0x01021994)return 2;
    char magic[8]; uint64_t size; rd(magic,8);rd(&size,8);
    if(memcmp(magic,"SQ2SPRS1",8)||size>32ULL*1024*1024*1024)return 2;
    int fd=open(argv[1],O_CREAT|O_EXCL|O_NOFOLLOW|O_WRONLY,0600);if(fd<0){perror("RAM file");return 4;}
    if(ftruncate(fd,size))return 4;
    char data[65536];
    for(;;){uint64_t off;uint32_t len;rd(&off,8);rd(&len,4);if(!len)break;if(len>sizeof data||off>size||len>size-off)return 5;
        rd(data,len);size_t done=0;while(done<len){ssize_t k=pwrite(fd,data+done,len-done,off+done);if(k<=0)return 6;done+=k;}}
    if(fsync(fd)||close(fd))return 7;
    printf("RAM_TEMPLATE_READY=%s size=%llu\n",argv[1],(unsigned long long)size);return 0;
}
