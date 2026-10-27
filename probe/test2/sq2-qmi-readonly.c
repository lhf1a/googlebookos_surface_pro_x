/* Read-only QRTR QMI diagnostics. Message IDs/layouts from libqmi data schemas:
 * https://github.com/linux-mobile-broadband/libqmi/tree/main/data
 * No radio enable, PIN, provisioning, persistent writes or data-call commands.
 */
#include <sys/socket.h>
#include <sys/time.h>
#include <linux/qrtr.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned le16(const uint8_t *p) { return p[0] | (unsigned)p[1]<<8; }
int main(int argc, char **argv) {
    if (argc != 4) { fprintf(stderr,"usage: %s node port uim|mode|revision|wds|audio-domain\n",argv[0]); return 2; }
    unsigned id;
    if (!strcmp(argv[3],"uim")) id=0x2f;
    else if (!strcmp(argv[3],"mode")) id=0x2d;
    else if (!strcmp(argv[3],"revision")) id=0x23;
    else if (!strcmp(argv[3],"wds")) id=0x22;
    else if (!strcmp(argv[3],"audio-domain")) id=0x21;
    else return 2;
    int fd=socket(AF_QIPCRTR, SOCK_DGRAM, 0);
    if(fd<0) { perror("socket"); return 1; }
    struct timeval tv={.tv_sec=5};
    if(setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv))) return 1;
    struct sockaddr_qrtr dst={.sq_family=AF_QIPCRTR,.sq_node=strtoul(argv[1],0,0),.sq_port=strtoul(argv[2],0,0)};
    uint8_t req[32]={0,1,0,id&255,id>>8,0,0}, buf[4096];
    size_t req_len=7;
    if(!strcmp(argv[3],"audio-domain")) {
        /* SERVREG_LOC_GET_DOMAIN_LIST, from upstream/pd-mapper/servreg_loc.c. */
        req[5]=12; req[7]=1; req[8]=9;
        memcpy(req+10,"avs/audio",9); req_len=19;
    }
    if(sendto(fd,req,req_len,0,(void*)&dst,sizeof(dst))!=(ssize_t)req_len) {perror("sendto"); return 1;}
    struct sockaddr_qrtr src; socklen_t sl=sizeof(src);
    ssize_t n=recvfrom(fd,buf,sizeof(buf),0,(void*)&src,&sl);
    close(fd);
    if(n<0) {perror("recvfrom"); return 1;}
    if(src.sq_node!=dst.sq_node || src.sq_port!=dst.sq_port || n<7 || buf[0]!=2 || le16(buf+1)!=1 || le16(buf+3)!=id || le16(buf+5)!=(unsigned)n-7) {fprintf(stderr,"Unexpected response\n");return 1;}
    printf("QMI %s response from node=%u port=%u, message=0x%04x\n",argv[3],src.sq_node,src.sq_port,id);
    int success=0;
    for(size_t p=7;p+3<=(size_t)n;) {
        unsigned type=buf[p],len=le16(buf+p+1); p+=3;
        if(p+len>(size_t)n) return 1;
        const uint8_t *v=buf+p;
        if(type==2 && len==4) {printf("result=%u error=%u\n",le16(v),le16(v+2));success=le16(v)==0;}
        if(!strcmp(argv[3],"audio-domain")) {
            printf("TLV %02x len=%u data=",type,len);
            for(unsigned i=0;i<len;i++) printf("%02x",v[i]);
            printf(" text="); for(unsigned i=0;i<len;i++) putchar(v[i]>=32&&v[i]<127?v[i]:'.'); putchar('\n');
        } else if(!strcmp(argv[3],"uim") && type==0x10 && len>=9) {
            printf("card_slots_reported=%u\n",v[8]);
            if(v[8] && len>=15) printf("first_card_state=%u (0=absent,1=present,2=error), apps=%u\n",v[9],v[14]);
        } else if(!strcmp(argv[3],"revision") && type==1) {
            printf("revision="); for(unsigned i=0;i<len;i++) putchar(v[i]>=32&&v[i]<127?v[i]:'?'); putchar('\n');
        } else if((!strcmp(argv[3],"mode") || !strcmp(argv[3],"wds")) && type==1 && len) printf("status=%u\n",v[0]);
        p+=len;
    }
    return success?0:1;
}
