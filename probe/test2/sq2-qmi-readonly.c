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
#include <errno.h>
static unsigned le16(const uint8_t *p) { return p[0] | (unsigned)p[1]<<8; }

static const char *mode_name(unsigned mode) {
    static const char *const names[] = {
        "online", "low-power", "factory-test", "offline", "reset",
        "shutting-down", "persistent-low-power", "mode-only-low-power"
    };
    return mode < sizeof(names)/sizeof(names[0]) ? names[mode] : "unknown";
}

/* Ignore operator/location/identity TLVs: LTE bring-up only needs states. */
static int decode_response(const uint8_t *buf, size_t n, unsigned id, const char *kind) {
    if(n<7 || buf[0]!=2 || le16(buf+1)!=1 || le16(buf+3)!=id || le16(buf+5)!=n-7)
        return 1;
    int result_seen=0, success=0, payload_seen=0;
    /* Validate the entire TLV envelope before decoding any payload. */
    for(size_t p=7;p<n;) {
        if(n-p<3) return 1;
        unsigned type=buf[p],len=le16(buf+p+1); p+=3;
        if(len>n-p) return 1;
        if(type==2) {
            if(len!=4 || result_seen) return 1;
            result_seen=1;
            success=le16(buf+p)==0 && le16(buf+p+2)==0;
            printf("result=%u error=%u\n",le16(buf+p),le16(buf+p+2));
        }
        p+=len;
    }
    if(!result_seen || !success) return 1;
    for(size_t p=7;p<n;) {
        unsigned type=buf[p],len=le16(buf+p+1); p+=3;
        const uint8_t *v=buf+p;
        if(!strcmp(kind,"audio-domain")) {
            printf("TLV %02x len=%u data=",type,len);
            for(unsigned i=0;i<len;i++) printf("%02x",v[i]);
            printf(" text="); for(unsigned i=0;i<len;i++) putchar(v[i]>=32&&v[i]<127?v[i]:'.'); putchar('\n');
        } else if(!strcmp(kind,"uim") && type==0x10) {
            if(len<9 || (v[8] && len<15)) return 1;
            payload_seen=1;
            printf("card_slots_reported=%u\n",v[8]);
            if(v[8]) printf("first_card_state=%u (0=absent,1=present,2=error), apps=%u\n",v[9],v[14]);
        } else if(!strcmp(kind,"revision") && type==1) {
            payload_seen=1;
            printf("revision="); for(unsigned i=0;i<len;i++) putchar(v[i]>=32&&v[i]<127?v[i]:'?'); putchar('\n');
        } else if(!strcmp(kind,"mode") && type==1) {
            if(len!=1) return 1;
            payload_seen=1;
            printf("status=%u\noperating_mode=%s\n",v[0],mode_name(v[0]));
        } else if(!strcmp(kind,"mode") && type==0x10) {
            if(len!=2) return 1;
            printf("offline_reason_mask=0x%04x\n",le16(v));
        } else if(!strcmp(kind,"mode") && type==0x11) {
            if(len!=1) return 1;
            printf("hardware_restricted=%u\n",v[0]);
        } else if(!strcmp(kind,"wds") && type==1) {
            if(len<1) return 1;
            payload_seen=1;
            printf("status=%u (1=disconnected,2=connected,3=suspended,4=authenticating)\n",v[0]);
        } else if(!strcmp(kind,"nas") && type==1) {
            if(len<5 || v[4]>len-5) return 1;
            payload_seen=1;
            printf("registration=%u (0=not-registered,1=registered,2=searching,3=denied,4=unknown)\n",v[0]);
            printf("cs_attach=%u ps_attach=%u (0=unknown,1=attached,2=detached)\n",v[1],v[2]);
            printf("selected_network=%u radio_interfaces=",v[3]);
            for(unsigned i=0;i<v[4];i++) printf("%s%u",i?",":"",v[5+i]);
            printf(" (8=LTE)\n");
        }
        p+=len;
    }
    return payload_seen || !strcmp(kind,"audio-domain") ? 0 : 1;
}

static int parse_endpoint(const char *text, uint32_t *value) {
    char *end;
    if(*text<'0' || *text>'9') return 1;
    errno=0;
    unsigned long parsed=strtoul(text,&end,10);
    if(errno || *end || parsed>=QRTR_PORT_CTRL) return 1;
    *value=(uint32_t)parsed;
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 4) { fprintf(stderr,"usage: %s node port uim|mode|revision|wds|nas|audio-domain\n",argv[0]); return 2; }
    unsigned id;
    if (!strcmp(argv[3],"uim")) id=0x2f;
    else if (!strcmp(argv[3],"mode")) id=0x2d;
    else if (!strcmp(argv[3],"revision")) id=0x23;
    else if (!strcmp(argv[3],"wds")) id=0x22;
    else if (!strcmp(argv[3],"nas")) id=0x24; /* NAS Get Serving System */
    else if (!strcmp(argv[3],"audio-domain")) id=0x21;
    else return 2;
    struct sockaddr_qrtr dst={.sq_family=AF_QIPCRTR};
    if(parse_endpoint(argv[1],&dst.sq_node) || parse_endpoint(argv[2],&dst.sq_port) || !dst.sq_port)
        return 2;
    int fd=socket(AF_QIPCRTR, SOCK_DGRAM, 0);
    if(fd<0) { perror("socket"); return 1; }
    struct timeval tv={.tv_sec=5};
    if(setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv))) { close(fd); return 1; }
    uint8_t req[32]={0,1,0,id&255,id>>8,0,0}, buf[4096];
    size_t req_len=7;
    if(!strcmp(argv[3],"audio-domain")) {
        /* SERVREG_LOC_GET_DOMAIN_LIST, from upstream/pd-mapper/servreg_loc.c. */
        req[5]=12; req[7]=1; req[8]=9;
        memcpy(req+10,"avs/audio",9); req_len=19;
    }
    if(sendto(fd,req,req_len,0,(void*)&dst,sizeof(dst))!=(ssize_t)req_len) {perror("sendto"); close(fd); return 1;}
    struct sockaddr_qrtr src={0}; socklen_t sl=sizeof(src);
    ssize_t n=recvfrom(fd,buf,sizeof(buf),MSG_TRUNC,(void*)&src,&sl);
    close(fd);
    if(n<0) {perror("recvfrom"); return 1;}
    if(sl!=sizeof(src) || src.sq_family!=AF_QIPCRTR || src.sq_node!=dst.sq_node || src.sq_port!=dst.sq_port || (size_t)n>sizeof(buf)) {fprintf(stderr,"Unexpected response\n");return 1;}
    printf("QMI %s response from node=%u port=%u, message=0x%04x\n",argv[3],src.sq_node,src.sq_port,id);
    return decode_response(buf,(size_t)n,id,argv[3]);
}
