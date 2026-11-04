/* SC8180X IPA embedded endpoint 1 / QMAPv4 mux 1 data session.
 * Keep each QMI client socket for its entire session; stop only our own handle.
 * APN is supplied by Android telephony, never written to NV.
 */
#include <sys/socket.h>
#include <linux/qrtr.h>
#include <poll.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <sys/prctl.h>
static unsigned g16(const unsigned char*p){return p[0]|(unsigned)p[1]<<8;}
static unsigned g32(const unsigned char*p){return g16(p)|g16(p+2)<<16;}
static void p16(unsigned char*p,unsigned v){p[0]=v;p[1]=v>>8;}
static void p32(unsigned char*p,unsigned v){p16(p,v);p16(p+2,v>>16);}
static long now_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000L+t.tv_nsec/1000000;}
struct reply {const unsigned char *v[256];unsigned z[256];};
static int decode(const unsigned char*b,int n,unsigned txn,unsigned msg,struct reply*r){
 memset(r,0,sizeof(*r));if(n<7||b[0]!=2||g16(b+1)!=txn||g16(b+3)!=msg||g16(b+5)!=(unsigned)n-7)return -1;
 for(int p=7;p<n;){if(n-p<3)return -1;unsigned t=b[p],z=g16(b+p+1);p+=3;if(z>(unsigned)(n-p)||r->v[t])return -1;r->v[t]=b+p;r->z[t]=z;p+=z;}
 if(r->z[2]!=4)return -1;
 if(g16(r->v[2])||g16(r->v[2]+2))fprintf(stderr,"QMI msg=%04x result=%u error=%u\n",msg,g16(r->v[2]),g16(r->v[2]+2));
 return g16(r->v[2])||g16(r->v[2]+2)?1:0;
}
static int open_port(const char *text){char*e;errno=0;unsigned long port=strtoul(text,&e,10);if(errno||*e||!port||port>=QRTR_PORT_CTRL)return -1;
 int fd=socket(AF_QIPCRTR,SOCK_DGRAM|SOCK_CLOEXEC,0);if(fd<0)return -1;struct sockaddr_qrtr d={.sq_family=AF_QIPCRTR,.sq_node=0,.sq_port=port};
 if(connect(fd,(void*)&d,sizeof(d))){close(fd);return -1;}return fd;
}
static int exchange(int fd,unsigned msg,const unsigned char*tlv,unsigned len,unsigned char*b,struct reply*r){
 memset(r,0,sizeof(*r));
 static unsigned txn;txn=(txn%65535)+1;unsigned char q[512]={0};if(len>sizeof(q)-7)return -1;
 p16(q+1,txn);p16(q+3,msg);p16(q+5,len);if(len)memcpy(q+7,tlv,len);
 if(send(fd,q,len+7,0)!=(ssize_t)(len+7))return -1;
 long end=now_ms()+15000;while(now_ms()<end){struct pollfd f={fd,POLLIN,0};int v=poll(&f,1,end-now_ms());if(v<0&&errno==EINTR)continue;if(v<=0)return -1;
 int n=recv(fd,b,16384,MSG_TRUNC);if(n<7||n>16384)return -1;if(b[0]!=2||g16(b+1)!=txn||g16(b+3)!=msg)continue;return decode(b,n,txn,msg,r);}
 return -1;
}
static volatile sig_atomic_t stopped;
static void on_signal(int sig){(void)sig;stopped=1;}
static unsigned tlv(unsigned char*b,unsigned pos,unsigned type,const void*v,unsigned z){if(pos+3+z>400)abort();b[pos]=type;p16(b+pos+1,z);memcpy(b+pos+3,v,z);return pos+3+z;}
static unsigned integer(unsigned char*b,unsigned pos,unsigned type,unsigned value,unsigned size){unsigned char v[4];p32(v,value);return tlv(b,pos,type,v,size);}
static void ipout(const char*name,unsigned v){printf("%s=%u.%u.%u.%u\n",name,v>>24,(v>>16)&255,(v>>8)&255,v&255);}
static int valid_apn(const char *s){size_t n=strlen(s);if(!n||n>100)return 0;for(size_t i=0;i<n;i++)if(!((s[i]>='a'&&s[i]<='z')||(s[i]>='A'&&s[i]<='Z')||(s[i]>='0'&&s[i]<='9')||s[i]=='.'||s[i]=='-'))return 0;return 1;}
/* Packet-service indications have no result TLV. Reject truncation/duplicates. */
static int packet_status(const unsigned char*b,int n){
 if(n<7||b[0]!=4||g16(b+3)!=0x22||g16(b+5)!=(unsigned)n-7)return -1;
 int status=-1;unsigned char seen[256]={0};
 for(int p=7;p<n;){if(n-p<3)return -1;unsigned t=b[p],z=g16(b+p+1);p+=3;if(z>(unsigned)(n-p)||seen[t]++)return -1;
  if(t==1){if(z!=2)return -1;status=b[p];}p+=z;}
 return status;
}
int main(int argc,char**argv){
 if(argc!=5||!valid_apn(argv[4]))return 2;
 setvbuf(stdout,NULL,_IOLBF,0);signal(SIGINT,on_signal);signal(SIGTERM,on_signal);signal(SIGHUP,on_signal);
 /* The HAL may close stdout before graceful QMI teardown. */
 signal(SIGPIPE,SIG_IGN);
 pid_t parent=getppid();if(parent==1||prctl(PR_SET_PDEATHSIG,SIGTERM)||getppid()!=parent)return 1;
 int dpm=open_port(argv[1]),wda=open_port(argv[2]),wds=open_port(argv[3]);if(dpm<0||wda<0||wds<0)return 1;
 unsigned char b[16384],q[400],ep[8];p32(ep,4);p32(ep+4,1);struct reply r;int ret=1,opened=0;unsigned handle=0,n;
 unsigned char hw[17]={1};p32(hw+1,4);p32(hw+5,1);p32(hw+9,2);p32(hw+13,11);
 n=tlv(q,0,0x11,hw,sizeof(hw));
 if(exchange(dpm,0x20,q,n,b,&r))goto done;opened=1;
 n=integer(q,0,0x10,0,1);n=integer(q,n,0x11,2,4);n=integer(q,n,0x12,8,4);n=integer(q,n,0x13,8,4);n=integer(q,n,0x15,32,4);n=integer(q,n,0x16,8192,4);n=tlv(q,n,0x17,ep,sizeof(ep));
 if(exchange(wda,0x20,q,n,b,&r))goto done;
 n=tlv(q,0,0x10,ep,sizeof(ep));if(exchange(wda,0x21,q,n,b,&r))goto done;
 if(r.z[0x11]!=4||g32(r.v[0x11])!=2||r.z[0x12]!=4||g32(r.v[0x12])!=8||r.z[0x13]!=4||g32(r.v[0x13])!=8){puts("FORMAT_MISMATCH");goto done;}
 n=tlv(q,0,0x10,ep,sizeof(ep));n=integer(q,n,0x11,1,1);if(exchange(wds,0xa2,q,n,b,&r))goto done;
 n=integer(q,0,1,4,1);if(exchange(wds,0x4d,q,n,b,&r))goto done;
 n=tlv(q,0,0x14,argv[4],strlen(argv[4]));n=integer(q,n,0x19,4,1);
 if(exchange(wds,0x20,q,n,b,&r)){if(r.z[0x10]==2)printf("CALL_END_REASON=%u\n",g16(r.v[0x10]));if(r.z[0x11]==4)printf("VERBOSE_CALL_END_TYPE=%u REASON=%u\n",g16(r.v[0x11]),g16(r.v[0x11]+2));goto done;}
 if(r.z[1]!=4||!(handle=g32(r.v[1])))goto done;
 puts("BEARER_STARTED=1");n=integer(q,0,0x10,(1u<<4)|(1u<<8)|(1u<<9)|(1u<<13)|(1u<<15),4);
 if(exchange(wds,0x2d,q,n,b,&r))goto done;
 const unsigned ids[]={0x1e,0x20,0x21,0x15,0x16};const char*names[]={"IP_ADDRESS","IP_GATEWAY","IP_NETMASK","DNS1","DNS2"};
 for(unsigned i=0;i<5;i++)if(r.z[ids[i]]==4)ipout(names[i],g32(r.v[ids[i]]));
 if(r.z[0x29]==4)printf("MTU=%u\n",g32(r.v[0x29]));
 if(r.z[0x1e]!=4||!g32(r.v[0x1e]))goto done;
 puts("BEARER_READY=1");ret=0;
 while(!stopped){struct pollfd f={wds,POLLIN,0};int ready=poll(&f,1,1000);if(ready<0&&errno==EINTR)continue;if(ready<0||(f.revents&(POLLERR|POLLHUP|POLLNVAL))){ret=1;break;}if(ready){int z=recv(wds,b,sizeof(b),MSG_TRUNC);if(z<7||z>(int)sizeof(b)){ret=1;break;}if(b[0]==4&&g16(b+3)==0x22){int status=packet_status(b,z);if(status!=2){printf("BEARER_DISCONNECTED=%d\n",status);ret=1;break;}}}}
 done:
 if(handle){n=integer(q,0,1,handle,4);if(exchange(wds,0x21,q,n,b,&r))ret=1;}
 if(opened){if(exchange(dpm,0x21,NULL,0,b,&r))ret=1;}
 close(dpm);close(wda);close(wds);printf("DATA_SESSION_EXIT=%d\n",ret);return ret;
}
