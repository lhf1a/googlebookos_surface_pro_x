/* A local in-memory QRTR service fixture: never opens a real socket. */
#define _POSIX_C_SOURCE 200809L
#include <sys/poll.h>
int fixture_poll(struct pollfd *fds,nfds_t count,int timeout);
#define main pdc_embedded_main
#define socket fixture_socket
#define sendto fixture_sendto
#define recvfrom fixture_recvfrom
#define poll fixture_poll
#define close fixture_close
#ifndef SQ2_PDC_SOURCE
#define SQ2_PDC_SOURCE "../probe/test2/sq2-pdc-own-profile-lab.c"
#endif
#include SQ2_PDC_SOURCE
#undef main
#undef socket
#undef sendto
#undef recvfrom
#undef poll
#undef close
static unsigned sockets,last_fd,used[100],token,transaction,previous_txn,duplicate_txn,step,frame;
static struct sockaddr_qrtr peer;
int fixture_socket(int domain,int type,int protocol) {
    if(domain!=AF_QIPCRTR||type!=SOCK_DGRAM||protocol)return -1;
    last_fd=10+(++sockets);return last_fd;
}
ssize_t fixture_sendto(int fd,const void *data,size_t len,int flags,const struct sockaddr *to,socklen_t size) {
    const uint8_t *r=data;
    if(fd<10||fd>=100||flags||size!=sizeof(peer)||len<60||r[3]!=0x26)return -1;
    memcpy(&peer,to,size);frame=le16(r+51);used[fd]+=frame;
    token=le32(r+len-4);transaction=le16(r+1);
    if(previous_txn==transaction)duplicate_txn++;
    previous_txn=transaction;last_fd=fd;step=0;return len;
}
int fixture_poll(struct pollfd *fds,nfds_t count,int timeout) {
    (void)timeout;
    if(count!=1||fds[0].fd!=(int)last_fd)return -1;
    return step<2?1:0;
}
ssize_t fixture_recvfrom(int fd,void *data,size_t capacity,int flags,struct sockaddr *from,socklen_t *size) {
    (void)flags;
    if(fd!=(int)last_fd||capacity<64||*size<sizeof(peer))return -1;
    memcpy(from,&peer,sizeof(peer));*size=sizeof(peer);
    uint8_t *b=data;memset(b,0,64);b[3]=0x26;
    if(step++==0) {
        b[0]=2;b[1]=transaction&255;b[2]=transaction>>8;b[5]=7;
        b[7]=2;b[8]=4;return 14;
    }
    b[0]=4;b[5]=26;b[7]=1;b[8]=2;
    b[12]=0x10;b[13]=4;put32(b+15,token);
    b[19]=0x11;b[20]=4;put32(b+22,frame);
    b[26]=0x12;b[27]=4;put32(b+29,20140-used[fd]);return 33;
}
int fixture_close(int fd) {(void)fd;return 0;}
int main(void) {
    struct sockaddr_qrtr dst={.sq_family=AF_QIPCRTR,.sq_node=0,.sq_port=27};
    struct profile_reply a={0},b={0};uint8_t chunk[1024]={0};
    int failed=profile_exchange(&dst,0x26,0,chunk,sizeof(chunk),&a)||
               profile_exchange(&dst,0x26,0,chunk,sizeof(chunk),&b);
    printf("FIXTURE sockets=%u duplicate_transactions=%u remaining=%u,%u\n",sockets,duplicate_txn,a.remaining,b.remaining);
    return failed||sockets!=1||duplicate_txn||a.remaining!=19116||b.remaining!=18092;
}
