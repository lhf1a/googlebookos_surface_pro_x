/* Fixed-scope QMI EFS service21 reader. Schema extracted from original SQ2
 * MPSS SHA9ec7b6c6, IDL 6/1.2, and checked with Qualcomm's IDL codec.
 * Only common metadata 0x1e/0x1f and read-shaped 0x21 are permitted.
 * Never construct 0x20 (path + data + flags + mode) or any RF/mode request.
 * File contents remain in a caller-selected private output directory. */
#define _POSIX_C_SOURCE 200809L
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <linux/qrtr.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
static unsigned u16(const unsigned char*p){return p[0]|(unsigned)p[1]<<8;}
static uint32_t u32(const unsigned char*p){return u16(p)|(uint32_t)u16(p+2)<<16;}
static void p16(unsigned char*p,unsigned v){p[0]=v;p[1]=v>>8;}
static void p32(unsigned char*p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=v>>(8*i);}
static long long ms(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;return (long long)t.tv_sec*1000+t.tv_nsec/1000000;}
static int number(const char*s,uint32_t*v){char*e;if(*s<'0'||*s>'9')return -1;errno=0;unsigned long x=strtoul(s,&e,10);if(errno||*e||x>=QRTR_PORT_CTRL)return -1;*v=x;return 0;}
static int mfs_request(unsigned char*b,unsigned msg,unsigned txn,const char*path){
    memset(b,0,1100);b[0]=0;p16(b+1,txn);p16(b+3,msg);unsigned n=0;
    if(msg==0x1e)n=0;
    else if(msg==0x1f){b[7]=1;p16(b+8,2);p16(b+10,0x21);n=5;}
    else if(msg==0x21){
        if(!path)return -1;size_t z=strlen(path)+1;if(z>1024)return -1;
        b[7]=1;p16(b+8,(unsigned)z+2);p16(b+10,(unsigned)z);memcpy(b+12,path,z);
        b[12+z]=0x10;p16(b+13+z,4);p32(b+15+z,32768);n=(unsigned)z+12;
    }
    else return -1;
    p16(b+5,n);return 7+(int)n;
}
struct decoded {unsigned result,error,fs_errno_present;uint32_t fs_errno;const unsigned char*data;unsigned len;unsigned tlv_types;};
static int decode(const unsigned char*b,size_t n,unsigned msg,unsigned txn,struct decoded*out){
    memset(out,0,sizeof(*out));
    if(n<7||b[0]!=2||u16(b+1)!=txn||u16(b+3)!=msg||u16(b+5)!=n-7)return -1;
    unsigned char seen[256]={0};
    for(size_t o=7;o<n;){if(n-o<3)return -1;unsigned t=b[o],z=u16(b+o+1);o+=3;
        if(seen[t]++||z>n-o)return -1;const unsigned char*v=b+o;
        if(t==2){if(z!=4)return -1;out->result=u16(v);out->error=u16(v+2);}
        else if(msg==0x21&&t==0x10){if(z!=4)return -1;out->fs_errno_present=1;out->fs_errno=u32(v);}
        else if(msg==0x21&&t==0x11){if(z<2||u16(v)!=z-2||z-2>32768)return -1;out->data=v+2;out->len=z-2;}
        else if(msg==0x1e&&t==0x10){if(z<2||u16(v)!=z-2)return -1;out->data=v+2;out->len=z-2;}
        else if(msg==0x1f&&t>=0x10&&t<=0x12){if(!z||v[0]!=z-1)return -1;out->tlv_types|=1u<<(t-0x10);}
        o+=z;
    }
    if(!seen[2]||out->result>1||(out->result==0&&out->error))return -1;
    return 0;
}
static int exchange(int fd,unsigned msg,unsigned txn,const char*path,unsigned char*buf,struct decoded*out){
    unsigned char req[1100];int n=mfs_request(req,msg,txn,path);if(n<0){puts("REQUEST_UNAVAILABLE");return -1;}
    printf("REQUEST msg=0x%04x txn=%u bytes=%d%s%s\n",msg,txn,n,path?" path=":"",path?path:"");
    if(send(fd,req,(size_t)n,0)!=n){perror("send");return -1;}
    long long deadline=ms()+3000;
    while(ms()<deadline){struct pollfd f={fd,POLLIN,0};int r=poll(&f,1,(int)(deadline-ms()));if(r<0&&errno==EINTR)continue;if(r<=0)break;
        ssize_t z=recv(fd,buf,34000,MSG_TRUNC);if(z<0||z>34000)return -1;
        if(z<7||buf[0]!=2||u16(buf+1)!=txn||u16(buf+3)!=msg)continue;
        if(decode(buf,(size_t)z,msg,txn,out)){puts("MALFORMED_RESPONSE");return -1;}
        printf("RESPONSE msg=0x%04x result=%u error=%u fs_errno_present=%u fs_errno=%u data_present=%u bytes=%u\n",msg,out->result,out->error,out->fs_errno_present,out->fs_errno,out->data!=NULL,out->len);
        return 0;
    }
    puts("RESPONSE_TIMEOUT; no file conclusion");return -1;
}
static int save_data(const char*dir,unsigned index,const unsigned char*b,unsigned n){
    char path[4096];int z=snprintf(path,sizeof(path),"%s/file-%02u.bin",dir,index);if(z<0||(size_t)z>=sizeof(path))return -1;
    int f=open(path,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);if(f<0)return -1;
    size_t done=0;while(done<n){ssize_t k=write(f,b+done,n-done);if(k<0&&errno==EINTR)continue;if(k<=0){close(f);return -1;}done+=(size_t)k;}
    return close(f);
}
static int selftest(void){
    unsigned char b[1100];int n=mfs_request(b,0x21,17,"/test");
    if(n<0){puts("BASELINE: EFS read request unavailable");return 1;}
    if(n!=25||u16(b+5)!=18||b[7]!=1||u16(b+8)!=8||u16(b+10)!=6||memcmp(b+12,"/test",6)||b[18]!=0x10||u32(b+21)!=32768)return 1;
    if(mfs_request(b,0x20,17,"/test")!=-1)return 1;
    unsigned char r[]={2,17,0,0x21,0,21,0,2,4,0,0,0,0,0,0x10,4,0,0,0,0,0,0x11,4,0,2,0,0xaa,0x55};
    struct decoded x;if(decode(r,sizeof(r),0x21,17,&x)||x.len!=2||x.data[0]!=0xaa)return 1;
    r[24]=3;if(decode(r,sizeof(r),0x21,17,&x)!=-1)return 1;r[24]=2;
    r[1]=18;if(decode(r,sizeof(r),0x21,17,&x)!=-1)return 1;
    puts("PASS: exact EFS read frame, strict response, wrong transaction/length rejected, write opcode refused");return 0;
}
int main(int argc,char**argv){
    setvbuf(stdout,NULL,_IOLBF,0);
    if(argc==2&&!strcmp(argv[1],"--self-test"))return selftest();
    if(argc==2&&!strcmp(argv[1],"--frame-fixture")){unsigned char b[1100];int n=mfs_request(b,0x21,17,"/test");if(n<0)return 1;for(int i=7;i<n;i++)printf("%02x",b[i]);puts("");return 0;}
    if(argc!=5||(strcmp(argv[1],"metadata")&&strcmp(argv[1],"inspect")))return 2;
    struct sockaddr_qrtr dst={.sq_family=AF_QIPCRTR};if(number(argv[2],&dst.sq_node)||number(argv[3],&dst.sq_port)||!dst.sq_port)return 2;
    struct stat st;if(stat(argv[4],&st)||!S_ISDIR(st.st_mode))return 2;
    int fd=socket(AF_QIPCRTR,SOCK_DGRAM|SOCK_CLOEXEC,0);if(fd<0)return 1;if(connect(fd,(void*)&dst,sizeof(dst))){close(fd);return 1;}
    unsigned char buf[34000];struct decoded x;int ret=1;unsigned txn=1;
    if(exchange(fd,0x1e,txn++,NULL,buf,&x))goto done;
    if(!x.result&&x.data){printf("SUPPORTED_MESSAGES=");for(unsigned i=0;i<x.len*8;i++)if(x.data[i/8]&(1u<<(i%8)))printf("%04x,",i);puts("");}
    if(exchange(fd,0x1f,txn++,NULL,buf,&x))goto done;
    if(!strcmp(argv[1],"metadata")){ret=0;goto done;}
    const char*paths[]={"/__sq2_lte_readonly_missing__","/nv/item_files/rfnv/00028874","/nv/item_store/rfnv/rfnv.bl","/nv/item_store/rfnv/rfc.bl","/rfc/radio_resources_selection_table_hw","/ftm_rf_debug_info.txt"};
    for(unsigned i=0;i<sizeof(paths)/sizeof(paths[0]);i++){
        if(exchange(fd,0x21,txn++,paths[i],buf,&x))goto done;
        if(!x.result&&(!x.fs_errno_present||!x.fs_errno)&&x.data){if(save_data(argv[4],i,x.data,x.len)){perror("save private response");goto done;}printf("SAVED_PRIVATE index=%u bytes=%u\n",i,x.len);}
        /* Stop if access is denied for the sentinel; do not bypass a policy. */
        if((x.fs_errno_present&&(x.fs_errno==1||x.fs_errno==13))||
           (i==0&&x.result&&x.error!=3&&x.error!=41)){puts("READ_ROUTE_REJECTED; stop");ret=3;goto done;}
    }
    ret=0;
done:close(fd);puts("CLEANUP QMI client closed; no RF/mode/NV write command");return ret;
}
