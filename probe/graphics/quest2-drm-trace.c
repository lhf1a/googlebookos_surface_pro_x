/* Private-process diagnostic only. Decode DRM import/info/submit UAPI without
 * altering requests. Explicit no-submit mode rejects MSM GEM_SUBMIT before it
 * reaches the kernel; this mode cannot prove rendering works. */
#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

struct submit_v0 {uint32_t flags,fence,nr_bos,nr_cmds;uint64_t bos,cmds;int32_t fence_fd;uint32_t queueid;};
struct submit_bo {uint32_t flags,handle;uint64_t presumed;};
struct submit_cmd {uint32_t type,index,offset,size,pad,nr_relocs;uint64_t relocs;};
struct bo_address {uint32_t handle;uint64_t address;};
static void dump_region(int fd,uint32_t handle,uint32_t offset,uint32_t size,
                        const struct bo_address *bos,unsigned nb,unsigned depth,unsigned *budget){
    if(!size||size>65536||offset>1048576||depth>4||size>*budget)return;
    *budget-=size;
    struct {uint32_t handle,info;uint64_t value;uint32_t len,pad;} info={handle,0,0,0,0};
    if(syscall(__NR_ioctl,fd,0xc0186443u,&info))return;
    size_t length=(size_t)offset+size;
    void *map=mmap(NULL,length,PROT_READ,MAP_SHARED,fd,(off_t)info.value);
    if(map==MAP_FAILED)return;
    const unsigned char *p=(const unsigned char*)map+offset;
    fprintf(stderr,"QUEST2_COMMAND_WORDS depth=%u handle=%u offset=%u",depth,handle,offset);
    for(uint32_t pos=0;pos+4<=size;pos+=4){uint32_t w;memcpy(&w,p+pos,4);fprintf(stderr," %08x",w);}
    fprintf(stderr,"\n");
    for(uint32_t pos=0;pos+4<=size;){
        uint32_t h;memcpy(&h,p+pos,4);
        unsigned type=h>>28,n=type==7?(h&0x3fff):(h&0x7f);
        if((type!=7&&type!=4)||n>(size-pos-4)/4)break;
        if(type==7&&((h>>16)&0x7f)==0x3f&&n==3){
            uint32_t args[3];memcpy(args,p+pos+4,12);
            uint64_t va=args[0]|((uint64_t)args[1]<<32);
            unsigned best=nb;
            for(unsigned j=0;j<nb;j++)if(bos[j].address&&bos[j].address<=va&&
                (best==nb||bos[j].address>bos[best].address))best=j;
            if(best<nb&&va-bos[best].address<=1048576 && args[2]<=16384)
                dump_region(fd,bos[best].handle,(uint32_t)(va-bos[best].address),args[2]*4,bos,nb,depth+1,budget);
        }
        pos+=(n+1)*4;
    }
    munmap(map,length);
}
static void dump_submit(int fd,const void *arg,unsigned size) {
    if(!arg || size<sizeof(struct submit_v0))return;
    struct submit_v0 s;memcpy(&s,arg,sizeof(s));
    fprintf(stderr,"QUEST2_SUBMIT flags=0x%x fence=%u bos=%u cmds=%u fence_fd=%d queue=%u\n",
            s.flags,s.fence,s.nr_bos,s.nr_cmds,s.fence_fd,s.queueid);
    if(s.nr_bos>256 || s.nr_cmds>16)return;
    struct bo_address addresses[256]={0};unsigned budget=262144;
    for(uint32_t i=0;s.bos && i<s.nr_bos;i++){
        struct submit_bo b;memcpy(&b,(const void*)(uintptr_t)(s.bos+i*sizeof(b)),sizeof(b));
        fprintf(stderr,"QUEST2_SUBMIT_BO i=%u handle=%u flags=0x%x iova=0x%" PRIx64 "\n",i,b.handle,b.flags,b.presumed);
        struct {uint32_t handle,info;uint64_t value;uint32_t len,pad;} info={b.handle,1,0,0,0};
        if(syscall(__NR_ioctl,fd,0xc0186443u,&info)==0)addresses[i]=(struct bo_address){b.handle,info.value};
    }
    for(uint32_t i=0;s.cmds && i<s.nr_cmds;i++){
        struct submit_cmd c;memcpy(&c,(const void*)(uintptr_t)(s.cmds+i*sizeof(c)),sizeof(c));
        fprintf(stderr,"QUEST2_SUBMIT_CMD i=%u type=%u index=%u offset=%u size=%u relocs=%u\n",
                i,c.type,c.index,c.offset,c.size,c.nr_relocs);
        // Only map this command range, never execute it. The bounded copy is
        // useful for decoding IB/layout registers against the firmware.
        if(c.index<s.nr_bos && s.bos && c.size && c.size<=65536 && c.offset<=1048576){
            struct submit_bo b;memcpy(&b,(const void*)(uintptr_t)(s.bos+c.index*sizeof(b)),sizeof(b));
            dump_region(fd,b.handle,c.offset,c.size,addresses,s.nr_bos,0,&budget);
        }
    }
}

int ioctl(int fd, int request, ...) {
    va_list args;va_start(args,request);void *arg=va_arg(args,void*);va_end(args);
    uint32_t req=(uint32_t)request;
    unsigned size=(req>>16)&0x3fff, nr=req&255;
    int selected=((req>>8)&255)==0x64 && (nr==0x2e||nr==0x43||nr==0x46||nr==0x47);
    unsigned char before[64]={0},after[64]={0};
    char link[64],path[256]={0};
    if(selected){
        snprintf(link,sizeof(link),"/proc/self/fd/%d",fd);
        ssize_t n=readlink(link,path,sizeof(path)-1);
        selected=n>0 && (!strcmp(path,"/dev/dri/card0")||!strcmp(path,"/dev/dri/renderD128"));
    }
    unsigned count=size<sizeof(before)?size:sizeof(before);
    if(selected && arg)memcpy(before,arg,count);
    const char *deny=getenv("SQ2_DRM_TRACE_NO_SUBMIT");
    if(selected && nr==0x46 && deny && !strcmp(deny,"1")){
        dump_submit(fd,arg,size);
        fprintf(stderr,"QUEST2_DRM_SUBMIT_REJECTED diagnostic-no-submit fd=%d\n",fd);
        errno=EPERM;return -1;
    }
    int result=(int)syscall(__NR_ioctl,fd,req,arg);
    int saved_errno=errno;
    if(selected){
        if(arg && result>=0)memcpy(after,arg,count);
        fprintf(stderr,"QUEST2_DRM fd=%d req=0x%08x result=%d errno=%d bytes=%u before=",
                fd,req,result,result<0?saved_errno:0,size);
        for(unsigned i=0;i<count;i++)fprintf(stderr,"%02x",before[i]);
        fprintf(stderr," after=");
        for(unsigned i=0;i<count;i++)fprintf(stderr,"%02x",after[i]);
        fprintf(stderr,"\n");
    }
    errno=saved_errno;return result;
}
