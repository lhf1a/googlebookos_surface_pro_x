/* Allocation-only check: real DMA-buf mappings and lifetime, no GPU submits. */
#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
int main(int argc,char **argv) {
    if(argc!=2 || argv[1][0]!='/')return 64;
    void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!lib){fprintf(stderr,"%s\n",dlerror());return 1;}
    int (*open_ion)(void)=dlsym(lib,"ion_open");
    int (*close_ion)(int)=dlsym(lib,"ion_close");
    int (*alloc)(int,size_t,size_t,unsigned,unsigned,int *)=dlsym(lib,"ion_alloc_fd");
    assert(open_ion && close_ion && alloc);
    const size_t bytes=16384;
    for(unsigned flags=0;flags<=1;flags++) {
        int control=open_ion(),fd=-1;assert(control>=0);
        assert(!alloc(control,bytes,0,1U<<25,flags,&fd) && fd>=0);
        struct stat st;assert(!fstat(fd,&st) && st.st_size==(off_t)bytes);
        int pinned=dup(fd);assert(pinned>=0 && !close(fd) && !close_ion(control));
        unsigned char *a=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_SHARED,pinned,0);
        unsigned char *b=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_SHARED,pinned,0);
        assert(a!=MAP_FAILED && b!=MAP_FAILED);
        struct dma_buf_sync sync={.flags=DMA_BUF_SYNC_START|DMA_BUF_SYNC_RW};
        assert(!ioctl(pinned,DMA_BUF_IOCTL_SYNC,&sync));
        for(size_t i=0;i<bytes;i++)assert(a[i]==0 && b[i]==0);
        for(size_t i=0;i<bytes;i++)a[i]=(unsigned char)(i*37+11);
        __sync_synchronize();
        for(size_t i=0;i<bytes;i++)assert(b[i]==(unsigned char)(i*37+11));
        sync.flags=DMA_BUF_SYNC_END|DMA_BUF_SYNC_RW;
        assert(!ioctl(pinned,DMA_BUF_IOCTL_SYNC,&sync));
        void *bad=mmap(NULL,bytes,PROT_READ,MAP_SHARED,pinned,bytes);
        assert(bad==MAP_FAILED);
        char path[64],line[256];snprintf(path,sizeof(path),"/proc/self/fdinfo/%d",pinned);
        FILE *info=fopen(path,"r");assert(info);
        while(fgets(line,sizeof(line),info))printf("FDINFO flags=%u %s",flags,line);
        fclose(info);
        assert(!close(pinned));
        for(size_t i=0;i<bytes;i++)assert(b[i]==(unsigned char)(i*37+11));
        assert(!munmap(a,bytes) && !munmap(b,bytes));
        printf("ION_MEMORY_PASS flags=%u bytes=%zu zero=1 shared_map=1 fd_lifetime=1 bounds=1\n",flags,bytes);
    }
    dlclose(lib);return 0;
}
