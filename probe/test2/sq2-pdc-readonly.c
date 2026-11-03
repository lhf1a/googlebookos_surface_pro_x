/* Manual PDC read-only readiness probe. Only Get Selected Config (0x22),
 * List Configs (0x24), Get Config Info (0x28); no mutation requests.
 * Schemas: linux-mobile-broadband/libqmi/data/qmi-service-pdc.json.
 * Configuration IDs stay private: print only presence, lengths and counts.
 */
#define _POSIX_C_SOURCE 200809L
#include <sys/socket.h>
#include <sys/poll.h>
#include <linux/qrtr.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

static unsigned le16(const uint8_t *p) { return p[0]|(unsigned)p[1]<<8; }
static uint32_t le32(const uint8_t *p) { return le16(p)|(uint32_t)le16(p+2)<<16; }
static void put32(uint8_t *p,uint32_t v) { for(unsigned i=0;i<4;i++) p[i]=v>>(8*i); }
struct config_entry { uint32_t type; uint8_t length,id[255]; };
static struct config_entry entries[32];
static unsigned n_entries;
static long long ms(void) {
    struct timespec t; if(clock_gettime(CLOCK_MONOTONIC,&t)) return -1;
    return (long long)t.tv_sec*1000+t.tv_nsec/1000000;
}
static int endpoint(const char *s,uint32_t *v) {
    char *e; if(*s<'0'||*s>'9') return 1; errno=0;
    unsigned long n=strtoul(s,&e,10);
    if(errno||*e||n>=QRTR_PORT_CTRL) return 1;
    *v=n; return 0;
}
static size_t request(uint8_t *b,unsigned msg,unsigned config,uint32_t token) {
    memset(b,0,21); b[1]=1; b[3]=msg; b[5]=14;
    b[7]=msg==0x22?1:0x11; b[8]=4; put32(b+10,config);
    b[14]=0x10; b[15]=4; put32(b+17,token); return 21;
}
static size_t request_info(uint8_t *b,const struct config_entry *e,uint32_t token) {
    size_t z=5u+e->length,n=7u+3u+z+7u;
    memset(b,0,n);b[1]=1;b[3]=0x28;b[5]=(n-7)&255;b[6]=(n-7)>>8;
    b[7]=1;b[8]=z&255;b[9]=z>>8;put32(b+10,e->type);
    b[14]=e->length;memcpy(b+15,e->id,e->length);
    b[10+z]=0x10;b[11+z]=4;put32(b+13+z,token);return n;
}
/* Returns -1 malformed, 0 success, 1 protocol rejection. Validate all fields
 * before output. Indications are asynchronous, not a successful RPC response.
 */
static int decode(const uint8_t *b,size_t n,unsigned msg,uint32_t token,int emit) {
    if(n<7||(b[0]!=2&&b[0]!=4)||le16(b+3)!=msg||le16(b+5)!=n-7||
       (b[0]==2&&le16(b+1)!=1)) return -1;
    int got=0; unsigned result=0,error=0,active=0,pending=0,alen=0,plen=0,count=0;
    unsigned token_seen=0,list_seen=0;
    const uint8_t *list=NULL,*desc=NULL;unsigned dlen=0,size_seen=0,ver_seen=0;
    uint32_t total=0,version=0;
    for(size_t p=7;p<n;) {
        if(n-p<3) return -1;
        unsigned t=b[p],z=le16(b+p+1); p+=3;
        if(z>n-p) return -1;
        const uint8_t *v=b+p;
        if((b[0]==2&&t==2)||(b[0]==4&&t==1)) {
            if(got++||z!=(b[0]==2?4u:2u)) return -1;
            if(b[0]==2) { result=le16(v); error=le16(v+2); }
            else { error=le16(v); result=error!=0; }
        } else if(t==0x10) {
            if(token_seen++||z!=4||le32(v)!=token) return -1;
        } else if(b[0]==4&&msg==0x22&&(t==0x11||t==0x12)) {
            if(!z||v[0]!=z-1) return -1;
            if(t==0x11) { if(active++) return -1; alen=v[0]; }
            else { if(pending++) return -1; plen=v[0]; }
        } else if(b[0]==4&&msg==0x24&&t==0x11) {
            if(list_seen++||!z) return -1;
            count=v[0]; if(count>32) return -1; size_t k=1;
            list=v;
            for(unsigned i=0;i<count;i++) {
                if(z-k<5||le32(v+k)>1) return -1;
                unsigned len=v[k+4]; k+=5;
                if(len>z-k) return -1;
                k+=len;
            }
            if(k!=z) return -1;
        } else if(b[0]==4&&msg==0x28) {
            if(t==0x11) { if(size_seen++||z!=4)return -1;total=le32(v); }
            else if(t==0x13) { if(ver_seen++||z!=4)return -1;version=le32(v); }
            else if(t==0x12) { if(desc||!z||v[0]!=z-1)return -1;desc=v+1;dlen=v[0]; }
        }
        p+=z;
    }
    if(!got) return -1;
    if(emit) {
        printf("PDC msg=0x%04x %s result=%u error=%u token_present=%u\n",
               msg,b[0]==2?"response":"indication",result,error,token_seen);
        if(b[0]==4&&!result&&!error) {
            if(msg==0x22) printf("active_id_present=%u active_id_bytes=%u pending_id_present=%u pending_id_bytes=%u\n",active,alen,pending,plen);
            else if(msg==0x24) printf("config_list_present=%u config_count=%u\n",list_seen,count);
            else {
                printf("config_size_present=%u bytes=%u version_present=%u version=0x%08x description=",size_seen,total,ver_seen,version);
                for(unsigned i=0;i<dlen;i++) {
                    unsigned c=desc[i];putchar((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||(c&&strchr(" ._/-",c))?(int)c:'?');
                }
                putchar('\n');
            }
        }
    }
    if(b[0]==4&&msg==0x24&&!result&&!error&&list) {
        size_t k=1;n_entries=count;
        for(unsigned i=0;i<count;i++) {
            entries[i].type=le32(list+k);entries[i].length=list[k+4];k+=5;
            memcpy(entries[i].id,list+k,entries[i].length);k+=entries[i].length;
        }
    }
    return result||error;
}
static int query(const struct sockaddr_qrtr *dst,unsigned msg,unsigned config,const struct config_entry *e,unsigned ordinal) {
    uint32_t token=0x53513200u+config*2+(msg==0x24)+ordinal*4;
    uint8_t req[280],buf[8192]; size_t len;
    if(msg==0x28&&e&&e->type==config) len=request_info(req,e,token);
    else if(msg==0x22||msg==0x24) len=request(req,msg,config,token);
    else return 1;
    int fd=socket(AF_QIPCRTR,SOCK_DGRAM,0); if(fd<0) {perror("socket");return 1;}
    int ret=1,response_seen=0,indication_seen=0;
    printf("QUERY config=%s operation=%s ordinal=%u\n",config?"software":"platform",msg==0x22?"get-selected":msg==0x24?"list":"info",ordinal);
    if(sendto(fd,req,len,0,(const void*)dst,sizeof(*dst))!=(ssize_t)len) {perror("sendto");goto done;}
    long long start=ms(); if(start<0) goto done;
    while(ms()<start+6000) {
        int left=(int)(start+6000-ms()); if(left<=0) break;
        struct pollfd pf={.fd=fd,.events=POLLIN};
        int r=poll(&pf,1,left); if(r<0&&errno==EINTR) continue; if(r<=0) break;
        struct sockaddr_qrtr src={0}; socklen_t sl=sizeof(src);
        ssize_t n=recvfrom(fd,buf,sizeof(buf),MSG_TRUNC,(void*)&src,&sl);
        if(n<0||(size_t)n>sizeof(buf)||sl!=sizeof(src)||src.sq_family!=AF_QIPCRTR||src.sq_node!=dst->sq_node||src.sq_port!=dst->sq_port) goto done;
        if(n<7||le16(buf+3)!=msg) continue;
        r=decode(buf,(size_t)n,msg,token,1); if(r) {if(r<0)fprintf(stderr,"Malformed PDC envelope\n");goto done;}
        if(buf[0]==2) response_seen=1; else indication_seen=1;
        if(response_seen&&indication_seen) {ret=0;break;}
    }
    if(!response_seen||!indication_seen) fprintf(stderr,"PDC response_seen=%d indication_seen=%d; no config conclusion\n",response_seen,indication_seen);
done: close(fd);return ret;
}
static int selftest(void) {
    uint8_t b[]={4,0,0,0x22,0,13,0, 1,2,0,0,0, 0x11,5,0,4,'T','E','S','T'};
    if(decode(b,sizeof(b),0x22,0,0)) return 1;
    b[15]=6; if(decode(b,sizeof(b),0x22,0,0)!=-1) return 1;
    b[15]=4; b[11]=52; if(decode(b,sizeof(b),0x22,0,0)!=1) return 1;
    b[11]=0; b[5]--; if(decode(b,sizeof(b),0x22,0,0)!=-1) return 1;
    uint8_t r[21]; request(r,0x22,0,0x53513200);
    if(r[3]!=0x22||r[7]!=1||le32(r+17)!=0x53513200) return 1;
    request(r,0x24,1,0x53513203);
    if(r[3]!=0x24||r[7]!=0x11||le32(r+10)!=1) return 1;
    struct config_entry e={.type=1,.length=4,.id={'T','E','S','T'}};
    uint8_t req[280];size_t n=request_info(req,&e,0x53513204);
    if(n!=26||req[3]!=0x28||req[8]!=9||req[14]!=4||memcmp(req+15,"TEST",4)||le32(req+22)!=0x53513204) return 1;
    puts("PASS:PDC only read requests; strict indication/length/error/request fixtures; bounded metadata IDs remain private");return 0;
}
int main(int argc,char **argv) {
    if(argc==2&&!strcmp(argv[1],"--self-test")) return selftest();
    struct sockaddr_qrtr dst={.sq_family=AF_QIPCRTR};
    if(argc!=3||endpoint(argv[1],&dst.sq_node)||endpoint(argv[2],&dst.sq_port)||!dst.sq_port) {fprintf(stderr,"usage: %s NODE PDC_PORT\n",argv[0]);return 2;}
    setvbuf(stdout,NULL,_IOLBF,0);int failed=0;
    for(unsigned c=0;c<2;c++) {
        failed|=query(&dst,0x22,c,NULL,0);
        n_entries=0;int listed=query(&dst,0x24,c,NULL,0);failed|=listed;
        if(!listed) {
            unsigned count=n_entries;
            for(unsigned i=0;i<count;i++) {
                if(entries[i].type!=c) {failed=1;continue;}
                failed|=query(&dst,0x28,c,&entries[i],i+1);
            }
        }
    }
    return failed;
}
