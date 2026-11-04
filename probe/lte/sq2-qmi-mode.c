/* Idempotent policy request: online or low-power, after own FSG supplies RF NV.
 * No reset, FTM, NV/provisioning write, APN or data call.
 * QMI DMS schema: linux-mobile-broadband/libqmi/data/qmi-service-dms.json.
 */
#include <sys/socket.h>
#include <sys/poll.h>
#include <linux/qrtr.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <signal.h>

static volatile sig_atomic_t interrupted;
static void stop(int sig) { (void)sig; interrupted=1; }
static unsigned le16(const uint8_t *p) { return p[0] | (unsigned)p[1]<<8; }
static int endpoint(const char *text, uint32_t *out) {
    char *end;
    if(*text<'0' || *text>'9') return 1;
    errno=0;
    unsigned long v=strtoul(text,&end,10);
    if(errno || *end || v>=QRTR_PORT_CTRL) return 1;
    *out=(uint32_t)v;
    return 0;
}
static long long now_ms(void) {
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)) return -1;
    return (long long)t.tv_sec*1000+t.tv_nsec/1000000;
}
static size_t request(uint8_t *out, uint16_t txn, int mode) {
    if(mode != -1 && mode != 0 && mode != 1) return 0;
    uint8_t bytes[]={0,txn&255,txn>>8,mode<0?0x2d:0x2e,0,0,0,1,1,0,(uint8_t)mode};
    size_t n=mode<0?7:sizeof(bytes);
    if(mode>=0) bytes[5]=4;
    memcpy(out,bytes,n);
    return n;
}
static int response(const uint8_t *buf, size_t n, unsigned txn, unsigned msg, int *mode) {
    if(n<7 || buf[0]!=2 || le16(buf+1)!=txn || le16(buf+3)!=msg || le16(buf+5)!=n-7)
        return 1;
    int found=0, have_mode=0;
    for(size_t p=7;p<n;) {
        if(n-p<3) return 1;
        unsigned type=buf[p],len=le16(buf+p+1); p+=3;
        if(len>n-p) return 1;
        if(type==2) {
            if(found || len!=4) return 1;
            found=1;
            printf("DMS message=0x%04x result=%u error=%u\n",msg,le16(buf+p),le16(buf+p+2));
            if(le16(buf+p) || le16(buf+p+2)) return 1;
        } else if(type==1 && msg==0x2d) {
            if(len!=1 || have_mode) return 1;
            *mode=buf[p]; have_mode=1;
        }
        p+=len;
    }
    return !found || (msg==0x2d && !have_mode);
}
static int exchange(const struct sockaddr_qrtr *dst, int set_mode, int *read_mode) {
    static uint16_t transaction;
    uint16_t txn=++transaction;
    uint8_t req[16],buf[4096];
    size_t len=request(req,txn,set_mode);
    int fd=socket(AF_QIPCRTR,SOCK_DGRAM,0),ret=1;
    if(fd<0) { perror("socket"); return 1; }
    if(sendto(fd,req,len,0,(const void*)dst,sizeof(*dst))!=(ssize_t)len) {
        perror("sendto"); goto done;
    }
    long long start=now_ms();
    if(start<0) goto done;
    for(;;) {
        long long remaining=start+5000-now_ms();
        if(remaining<=0) { fprintf(stderr,"DMS timeout\n"); break; }
        struct pollfd pollfd={.fd=fd,.events=POLLIN};
        int ready=poll(&pollfd,1,(int)remaining);
        if(ready<0 && errno==EINTR) continue;
        if(ready<=0) { fprintf(stderr,"DMS poll timeout/error\n"); break; }
        struct sockaddr_qrtr src={0}; socklen_t size=sizeof(src);
        ssize_t n=recvfrom(fd,buf,sizeof(buf),MSG_TRUNC,(void*)&src,&size);
        if(n<0 || (size_t)n>sizeof(buf)) break;
        if(size!=sizeof(src) || src.sq_family!=AF_QIPCRTR ||
           src.sq_node!=dst->sq_node || src.sq_port!=dst->sq_port) continue;
        if(n<7 || buf[0]!=2 || le16(buf+1)!=txn) continue;
        ret=response(buf,(size_t)n,txn,set_mode<0?0x2d:0x2e,read_mode);
        break;
    }
done:
    close(fd);
    return ret;
}
int main(int argc,char **argv) {
 if(argc==2&&!strcmp(argv[1],"selftest")) {
  uint8_t b[16];if(request(b,1,0)!=11||b[3]!=0x2e||b[10]||request(b,2,-1)!=7||b[3]!=0x2d)return 1;
  if(request(b,1,1)!=11||b[10]!=1)return 1;
  for(int m=2;m<256;m++)if(request(b,1,m))return 1;
  uint8_t r[]={2,1,0,0x2e,0,7,0,2,4,0,0,0,0,0};int v=-1;
  if(response(r,sizeof(r),1,0x2e,&v))return 1;r[12]=52;if(!response(r,sizeof(r),1,0x2e,&v))return 1;
  if(!response(r,sizeof(r)-1,1,0x2e,&v))return 1;
  puts("PASS online/low-power builder, refusal and malformed reply");return 0;
 }
 struct sockaddr_qrtr dst={.sq_family=AF_QIPCRTR};
 if(argc!=4||(strcmp(argv[3],"online")&&strcmp(argv[3],"low-power"))||endpoint(argv[1],&dst.sq_node)||endpoint(argv[2],&dst.sq_port)||!dst.sq_port)return 2;
 int desired=!strcmp(argv[3],"low-power");
 setvbuf(stdout,NULL,_IOLBF,0);signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGHUP,stop);
 int original=-1,observed=-1;
 if(exchange(&dst,-1,&original)||(original!=0&&original!=1&&original!=5)){fprintf(stderr,"Refusing unsupported modem mode %d\n",original);return 1;}
 if(original==desired){printf("FINAL_MODE=%d\n",original);return 0;}
 if(interrupted)return 1;
 int failed=exchange(&dst,desired,&observed);
 /* Resolve uncertain completion only by reading; never repeat the write. */
 if(exchange(&dst,-1,&observed))return 1;
 /* The standard set-mode ACK precedes the asynchronous radio transition. */
 for(int i=0;!failed&&!interrupted&&observed!=desired&&i<5;i++){
  sleep(1);if(exchange(&dst,-1,&observed))return 1;
 }
 printf("FINAL_MODE=%d request_failed=%d\n",observed,failed);
 return failed||observed!=desired;
}
