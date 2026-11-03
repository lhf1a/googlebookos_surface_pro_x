/* Lab-only bounded DIAG observer, not a production boot component.
 * Protocol references (not linked): linux-msm/diag 23c12c1,
 * catxeon/qcom-efs-browser 9fde4f9; project docs/LTE-DATA.md.
 * No arbitrary payload input, NV writes, file reads/writes, logging masks,
 * modem reset, mode switch or RF configuration commands are implemented.
 */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <linux/qrtr.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static uint16_t g16(const uint8_t *p) { return p[0] | (uint16_t)p[1]<<8; }
static uint32_t g32(const uint8_t *p) { return g16(p) | (uint32_t)g16(p+2)<<16; }
static void p32(uint8_t *p, uint32_t x) { for(int i=0;i<4;i++) p[i]=(uint8_t)(x>>(8*i)); }
static int64_t now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1000LL+t.tv_nsec/1000000; }
static int fd[4]={-1,-1,-1,-1}; /* DATA2, DCI4, CMD lookup1, CTRL0 */
static struct sockaddr_qrtr local[4], cmd;
static int have_cmd, features;
static int64_t deadline;
static unsigned ctrl_count, dropped;
static void cleanup(void) { for(int i=0;i<4;i++) if(fd[i]>=0) { close(fd[i]);fd[i]=-1; } }
static void fail(const char *s) { perror(s);exit(1); }

static int open_sock(struct sockaddr_qrtr *a) {
    int s=socket(AF_QIPCRTR,SOCK_DGRAM|SOCK_CLOEXEC,0);
    if(s<0) fail("socket");
    socklen_t n=sizeof(*a); memset(a,0,n);
    if(getsockname(s,(void*)a,&n)) fail("getsockname");
    a->sq_family=AF_QIPCRTR; a->sq_port=0;
    if(bind(s,(void*)a,sizeof(*a))) fail("bind local");
    if(getsockname(s,(void*)a,&n)) fail("getsockname bound");
    return s;
}
static void announce(int i,uint32_t type,uint32_t instance) {
    uint8_t b[20]={0};p32(b,type);p32(b+4,4097);p32(b+8,instance);
    if(type==QRTR_TYPE_NEW_SERVER) { p32(b+12,local[i].sq_node);p32(b+16,local[i].sq_port); }
    struct sockaddr_qrtr a=local[i];a.sq_port=QRTR_PORT_CTRL;
    if(sendto(fd[i],b,sizeof(b),0,(void*)&a,sizeof(a))!=sizeof(b))fail("announce");
    printf("AP %s instance=%u at=%u:%u\n",type==QRTR_TYPE_NEW_SERVER?"publish":"lookup",instance,local[i].sq_node,local[i].sq_port);
}

static int unframe(const uint8_t *b,size_t n,const uint8_t **out) {
    if(n<5 || b[0]!=0x7e || b[1]!=1)return -1;
    size_t len=g16(b+2);
    if(len+5!=n || b[len+4]!=0x7e)return -1;
    *out=b+4;return (int)len;
}
static int feature_response(const uint8_t *b,size_t n,uint8_t out[15]) {
    if(n<15 || g32(b)!=8 || g32(b+4)!=n-8 || g32(b+8)!=3)return 0;
    /* Only feature exchange, request/response, raw requests and QRTR. */
    uint32_t peer=b[12]|(uint32_t)b[13]<<8|(uint32_t)b[14]<<16;
    uint32_t common=peer & 0x2051;
    if((common & 0x2051)!=0x2051)return 0;
    memset(out,0,15);p32(out,8);p32(out+4,7);p32(out+8,3);
    out[12]=(uint8_t)common;out[13]=(uint8_t)(common>>8);out[14]=(uint8_t)(common>>16);
    return 1;
}
static void control(const uint8_t *b,size_t n,const struct sockaddr_qrtr *a) {
    size_t off=0;
    while(n-off>=8) {
        uint32_t type=g32(b+off),len=g32(b+off+4);
        if(len>n-off-8) { dropped++;return; }
        ctrl_count++;
        if(ctrl_count<=12 || type==8)printf("CTRL type=%u bytes=%u peer=%u:%u\n",type,len,a->sq_node,a->sq_port);
        if(type==8 && !features) {
            uint8_t reply[15];
            if(!feature_response(b+off,len+8,reply)) {
                fprintf(stderr,"unsupported feature mask; no fallback negotiation\n");exit(3);
            }
            if(sendto(fd[3],reply,sizeof(reply),0,(const void*)a,sizeof(*a))!=sizeof(reply)) fail("feature reply");
            features=1;printf("feature negotiated bytes=3 common=%02x%02x%02x\n",reply[14],reply[13],reply[12]);
        }
        off+=len+8;
    }
    if(off!=n)dropped++;
}
/* Returns one bounded framed CMD payload; logs never contain opaque payloads. */
static int pump(uint8_t *out,size_t cap,int64_t end) {
    while(now()<end && now()<deadline) {
        struct pollfd p[4]; for(int i=0;i<4;i++)p[i]=(struct pollfd){fd[i],POLLIN,0};
        int r=poll(p,4,100);if(r<0){if(errno==EINTR)continue;fail("poll");}
        for(int i=0;i<4;i++) if(p[i].revents&POLLIN) {
            uint8_t b[16384];struct sockaddr_qrtr a;socklen_t an=sizeof(a);
            ssize_t n=recvfrom(fd[i],b,sizeof(b),0,(void*)&a,&an);
            if(n<0)fail("recvfrom");
            if(a.sq_port==QRTR_PORT_CTRL) {
                if(i==2 && n==20 && g32(b)==QRTR_TYPE_NEW_SERVER && g32(b+4)==4097 && g32(b+8)==1 && g32(b+12)==0 && g32(b+16)!=0) {
                    cmd=(struct sockaddr_qrtr){.sq_family=AF_QIPCRTR,.sq_node=0,.sq_port=g32(b+16)};
                    have_cmd=1;printf("MODEM CMD=%u:%u\n",cmd.sq_node,cmd.sq_port);
                }
                continue;
            }
            if(a.sq_node!=0){dropped++;continue;}
            if(i==3)control(b,(size_t)n,&a);
            else if(i==2 && have_cmd && a.sq_port==cmd.sq_port) {
                const uint8_t *payload;int z=unframe(b,(size_t)n,&payload);
                if(z>=0 && (size_t)z<=cap){memcpy(out,payload,(size_t)z);return z;}
                dropped++;
            }
        }
        if(!out && features && have_cmd)return 0;
    }
    return -1;
}
static uint32_t dir_handle=0xffffffff;
static uint8_t method;
static int allowed(const uint8_t *b,size_t n) {
    if(n==133 && b[0]==0x26 && g16(b+1)==453) {
        for(size_t i=3;i<n;i++)if(b[i])return 0;
        return 1;
    }
    if(n<4 || b[0]!=0x4b || (b[1]!=0x3e && b[1]!=0x13) || b[3])return 0;
    if(b[2]==0 && n==44) {
        for(int i=0;i<6;i++)if(g32(b+4+i*4)!=0x100000)return 0;
        return g32(b+28)==1 && g32(b+32)==1 && g32(b+36)==1 && g32(b+40)==0xffffffff;
    }
    if(b[1]!=method)return 0;
    if(b[2]==11) { static const char path[]="/nv/item_files/rfnv";return n==4+sizeof(path) && !memcmp(b+4,path,sizeof(path)); }
    if(dir_handle==0xffffffff)return 0;
    if(b[2]==12 && n==12)return g32(b+4)==dir_handle && g32(b+8)>=1 && g32(b+8)<=128;
    if(b[2]==13 && n==8)return g32(b+4)==dir_handle;
    return 0;
}
static int exchange(const uint8_t *q,size_t n,uint8_t *r,size_t cap) {
    if(!features || !have_cmd || !allowed(q,n)) { fprintf(stderr,"request denied locally\n");return -1; }
    if(now()>=deadline)return -1;
    if(sendto(fd[2],q,n,0,(void*)&cmd,sizeof(cmd))!=(ssize_t)n)fail("request");
    int64_t end=now()+1800;
    while(now()<end) {
        int z=pump(r,cap,end);if(z<0)return -1;
        if(q[0]==0x26 && z>=3 && r[0]==0x26 && g16(r+1)==453)return z;
        if(q[0]==0x4b && z>=4 && !memcmp(q,r,4))return z;
        if(z>0 && (r[0]==0x13 || r[0]==0x14 || r[0]==0x15 || r[0]==0x18)) {
            printf("DIAG refused opcode=0x%02x response=0x%02x bytes=%d\n",q[0],r[0],z);return -1;
        }
    }
    return -1;
}
static void list_rf(void) {
    uint8_t q[256]={0},r[2048];int z=-1;
    for(int j=0;j<2;j++) {
        memset(q,0,sizeof(q));q[0]=0x4b;q[1]=j?0x13:0x3e;
        for(int i=0;i<6;i++)p32(q+4+i*4,0x100000);
        p32(q+28,1);p32(q+32,1);p32(q+36,1);p32(q+40,0xffffffff);
        z=exchange(q,44,r,sizeof(r));
        if(z>=44){method=q[1];printf("EFS HELLO subsystem=0x%02x\n",method);break;}
    }
    if(!method){puts("rfnv=unavailable (HELLO)");return;}
    memset(q,0,sizeof(q));q[0]=0x4b;q[1]=method;q[2]=11;
    const char path[]="/nv/item_files/rfnv";memcpy(q+4,path,sizeof(path));
    z=exchange(q,4+sizeof(path),r,sizeof(r));
    if(z<12){puts("rfnv=unavailable (OPENDIR)");return;}
    printf("rfnv opendir_errno=%u\n",g32(r+8));
    if(g32(r+8) || g32(r+4)==0xffffffff)return;
    dir_handle=g32(r+4);unsigned count=0;int complete=0;
    for(unsigned seq=1;seq<=128 && now()<deadline-1900;seq++) {
        q[2]=12;p32(q+4,dir_handle);p32(q+8,seq);
        z=exchange(q,12,r,sizeof(r));
        if(z<40 || g32(r+4)!=dir_handle || g32(r+8)!=seq)break;
        if(g32(r+12)){printf("rfnv readdir_errno=%u\n",g32(r+12));break;}
        if(z==40 || !r[40]){complete=1;break;}
        const uint8_t *nul=memchr(r+40,0,(size_t)z-40);if(!nul)break;
        size_t len=(size_t)(nul-r-40);
        if((len==1 && r[40]=='.') || (len==2 && r[40]=='.' && r[41]=='.'))continue;
        count++; /* Count only: no names or item contents exported. */
    }
    q[2]=13;p32(q+4,dir_handle);z=exchange(q,8,r,sizeof(r));
    printf("rfnv entries=%u complete=%d close_errno=%d\n",count,complete,z>=8?(int)g32(r+4):-1);
}
static int selftest(void) {
    const uint8_t *p;uint8_t frame[]={0x7e,1,1,0,0x26,0x7e};
    assert(unframe(frame,sizeof(frame),&p)==1 && p[0]==0x26);
    for(size_t n=0;n<sizeof(frame);n++)assert(unframe(frame,n,&p)<0);
    frame[2]=255;assert(unframe(frame,sizeof(frame),&p)<0);
    uint8_t f[15]={8,0,0,0,7,0,0,0,3,0,0,0,0x51,0x20,0},o[15];
    assert(feature_response(f,15,o));for(size_t n=0;n<15;n++)assert(!feature_response(f,n,o));
    f[8]=4;assert(!feature_response(f,15,o));
    uint8_t q[133]={0x26,0xc5,1};assert(allowed(q,133));
    q[0]=0x27;assert(!allowed(q,133));q[0]=0x26;q[1]=0;assert(!allowed(q,133));
    for(int op=0;op<256;op++){uint8_t a[12]={0x4b,0x13,(uint8_t)op};assert(!allowed(a,12));}
    puts("PASS frame bounds, 3-byte features, NV453-only whitelist, write/unknown rejection");return 0;
}
int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IOLBF,0);
    if(argc==2 && !strcmp(argv[1],"selftest"))return selftest();
    if(argc!=2 || strcmp(argv[1],"observe"))return 2;
    atexit(cleanup);deadline=now()+20000;
    for(int i=0;i<4;i++)fd[i]=open_sock(&local[i]);
    announce(0,QRTR_TYPE_NEW_SERVER,2);announce(1,QRTR_TYPE_NEW_SERVER,4);
    announce(2,QRTR_TYPE_NEW_LOOKUP,1);announce(3,QRTR_TYPE_NEW_SERVER,0);
    if(pump(NULL,0,now()+8000)<0 || !features || !have_cmd) {
        printf("handshake incomplete features=%d CMD=%d controls=%u dropped=%u\n",features,have_cmd,ctrl_count,dropped);return 3;
    }
    uint8_t q[133]={0x26,0xc5,1},r[2048];int z=exchange(q,sizeof(q),r,sizeof(r));
    if(z!=133){printf("NV453 unreadable bytes=%d\n",z);return 4;}
    printf("NV453 status=%u",g16(r+131));if(!g16(r+131))printf(" value=%u",r[3]);puts("");
    list_rf();printf("session complete controls=%u dropped=%u no NV writes\n",ctrl_count,dropped);return 0;
}
