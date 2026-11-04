/* One fixed RMNET link, matching the live IPA MAPv4 offload format. */
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/if_link.h>
#include <net/if.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
struct request {struct nlmsghdr n;struct ifinfomsg i;unsigned char data[256];};
static struct rtattr *add(struct request*q,unsigned type,const void*b,unsigned z){
 unsigned pos=NLMSG_ALIGN(q->n.nlmsg_len),size=RTA_LENGTH(z);if(pos+RTA_ALIGN(size)>sizeof(*q))abort();
 struct rtattr*r=(void*)((char*)q+pos);r->rta_type=type;r->rta_len=size;if(z)memcpy(RTA_DATA(r),b,z);q->n.nlmsg_len=pos+RTA_ALIGN(size);return r;
}
static void nest_end(struct request*q,struct rtattr*r){r->rta_len=(char*)q+q->n.nlmsg_len-(char*)r;}
static void build(struct request*q,unsigned parent){
 memset(q,0,sizeof(*q));q->n.nlmsg_len=NLMSG_LENGTH(sizeof(q->i));q->n.nlmsg_type=RTM_NEWLINK;q->n.nlmsg_flags=NLM_F_REQUEST|NLM_F_ACK|NLM_F_CREATE|NLM_F_EXCL;q->n.nlmsg_seq=1;q->i.ifi_family=AF_UNSPEC;
 add(q,IFLA_LINK,&parent,4);add(q,IFLA_IFNAME,"rmnet_data0",12);
 struct rtattr*info=add(q,IFLA_LINKINFO,NULL,0);add(q,IFLA_INFO_KIND,"rmnet",6);
 struct rtattr*data=add(q,IFLA_INFO_DATA,NULL,0);uint16_t mux=1;struct ifla_rmnet_flags f={.flags=15,.mask=63};
 add(q,IFLA_RMNET_MUX_ID,&mux,2);add(q,IFLA_RMNET_FLAGS,&f,sizeof(f));nest_end(q,data);nest_end(q,info);
}
int main(int argc,char**argv){
 struct request q;
 if(argc!=2||strcmp(argv[1],"create"))return 2;
 unsigned parent=if_nametoindex("rmnet_ipa0");if(!parent||if_nametoindex("rmnet_data0")){fputs("Refusing missing parent or existing child\n",stderr);return 1;}build(&q,parent);
 int fd=socket(AF_NETLINK,SOCK_RAW|SOCK_CLOEXEC,NETLINK_ROUTE);if(fd<0)return 1;struct sockaddr_nl dst={.nl_family=AF_NETLINK};if(sendto(fd,&q,q.n.nlmsg_len,0,(void*)&dst,sizeof(dst))!=(ssize_t)q.n.nlmsg_len)return 1;
 struct pollfd p={fd,POLLIN,0};if(poll(&p,1,3000)<=0)return 1;unsigned char b[4096];int n=recv(fd,b,sizeof(b),MSG_TRUNC);close(fd);if(n<(int)NLMSG_LENGTH(sizeof(struct nlmsgerr))||n>(int)sizeof(b))return 1;
 struct nlmsghdr*h=(void*)b;if(h->nlmsg_type!=NLMSG_ERROR||h->nlmsg_seq!=1||h->nlmsg_len>(unsigned)n)return 1;struct nlmsgerr*e=NLMSG_DATA(h);if(e->error){fprintf(stderr,"netlink: %s\n",strerror(-e->error));return 1;}
 return 0;
}
