/* SPDX-License-Identifier: MIT
 * Explicit development client; never installed by the normal bundle builder. */
#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
static void sync_cpu(int fd, uint64_t flags) {
    struct dma_buf_sync s={.flags=flags}; int r;
    do {r=ioctl(fd,DMA_BUF_IOCTL_SYNC,&s);} while(r<0 && errno==EINTR);
    assert(!r);
}
int main(int argc,char **argv) {
    assert(argc==2);
    void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL); if(!lib) {puts(dlerror());return 1;}
    int (*ion_open)(void)=dlsym(lib,"ion_open");
    int (*ion_alloc)(int,size_t,size_t,unsigned,unsigned,int *)=dlsym(lib,"ion_alloc_fd");
    assert(ion_open && ion_alloc); int control=ion_open(); assert(control>=0);
    const size_t sizes[]={1,4097,1048576+4096};
    for(unsigned cached=0;cached<2;cached++) for(unsigned round=0;round<8;round++) for(unsigned z=0;z<3;z++) {
        size_t n=sizes[z]; int fd=-1;
        assert(!ion_alloc(control,n,0,1U<<25,cached,&fd));
        char path[64],info[1024]={0}; snprintf(path,sizeof(path),"/proc/self/fdinfo/%d",fd);
        FILE *f=fopen(path,"r"); assert(f); size_t count=fread(info,1,sizeof(info)-1,f); assert(count); fclose(f);
        assert(strstr(info,cached?"exp_name:\tsystem\n":"exp_name:\tsystem-uncached\n"));
        uint8_t *p=mmap(NULL,n,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0); assert(p!=MAP_FAILED);
        sync_cpu(fd,DMA_BUF_SYNC_START|DMA_BUF_SYNC_RW);
        for(size_t i=0;i<n;i++) assert(!p[i]);
        for(size_t i=0;i<n;i++) p[i]=(uint8_t)(i*17+round+1);
        sync_cpu(fd,DMA_BUF_SYNC_END|DMA_BUF_SYNC_RW);
        int alias=dup(fd); assert(alias>=0); assert(!close(fd));
        pid_t child=fork(); assert(child>=0);
        if(!child) {
            assert(!munmap(p,n));
            p=mmap(NULL,n,PROT_READ|PROT_WRITE,MAP_SHARED,alias,0); assert(p!=MAP_FAILED);
            sync_cpu(alias,DMA_BUF_SYNC_START|DMA_BUF_SYNC_RW);
            for(size_t i=0;i<n;i++) {assert(p[i]==(uint8_t)(i*17+round+1)); p[i]^=0xa5;}
            sync_cpu(alias,DMA_BUF_SYNC_END|DMA_BUF_SYNC_RW);
            assert(!munmap(p,n)); assert(!close(alias)); _exit(0);
        }
        int status; assert(waitpid(child,&status,0)==child && WIFEXITED(status) && !WEXITSTATUS(status));
        sync_cpu(alias,DMA_BUF_SYNC_START|DMA_BUF_SYNC_READ);
        for(size_t i=0;i<n;i++) assert(p[i]==((uint8_t)(i*17+round+1)^0xa5));
        sync_cpu(alias,DMA_BUF_SYNC_END|DMA_BUF_SYNC_READ);
        assert(!close(alias)); assert(!munmap(p,n));
    }
    assert(!close(control)); assert(!dlclose(lib));
    puts("SYSTEM_HEAP_CHECK_PASS allocations=48 zeroing=48 fork_reimport=48 dup_close=48");
    return 0;
}
