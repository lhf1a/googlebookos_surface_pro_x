/* SQ2: bridge a real Linux HCI_USER controller to the Android serial H4 HAL.
 * No simulated controller or fabricated HCI replies. Linux owns UART firmware
 * setup; Android owns HCI commands after the exclusive USER-channel bind.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#define BTPROTO_HCI 1
#define HCIDEVUP _IOW('H', 201, int)
#define HCIDEVDOWN _IOW('H', 202, int)
struct sockaddr_hci { sa_family_t family; uint16_t dev, channel; };
static volatile sig_atomic_t stopping;
static void stop(int sig) { (void)sig; stopping=1; }
static void die(const char *s) { perror(s); exit(1); }
static int allwrite(int fd, const void *buf, size_t n) {
    const char *p=buf;
    while (n && !stopping) {
        ssize_t k=write(fd,p,n);
        if (k<0 && errno==EINTR) continue;
        if (k<=0) return -1;
        p+=k; n-=(size_t)k;
    }
    return n ? -1 : 0;
}
int main(int argc, char **argv) {
    if (argc!=3) { fprintf(stderr,"usage: %s hci-index --probe|/dev/sq2-bt-h4\n",argv[0]); return 2; }
    char *end; long dev=strtol(argv[1],&end,10);
    if (*end || dev<0 || dev>65534) return 2;
    setvbuf(stdout,NULL,_IOLBF,0);
    int hci=socket(AF_BLUETOOTH,SOCK_RAW|SOCK_CLOEXEC,BTPROTO_HCI);
    if (hci<0) die("HCI socket");
    if (!strcmp(argv[2],"--probe")) {
        if (ioctl(hci,HCIDEVUP,dev)<0 && errno!=EALREADY) die("HCIDEVUP");
        puts("SQ2_HCI_UP: real controller initialized by kernel");
        close(hci); return 0;
    }
    struct sockaddr_hci addr={AF_BLUETOOTH,(uint16_t)dev,1};
    /* Kernel setup is asynchronous; hci0 existing does not mean HCI_SETUP
     * has cleared. Never issue HCIDEVDOWN before binding: it cancels/waits
     * for setup and closes a controller we are about to reopen. HCI_USER
     * accepts an initialized AUTO_OFF controller without that extra reset.
     * Do not unload/reset the live QCA UART driver to retry this bind.
     */
    printf("SQ2_HCI_WAIT_SETUP hci%ld (no forced close)\n", dev);
    for (unsigned attempt=0;;attempt++) {
        if (bind(hci,(struct sockaddr *)&addr,sizeof(addr))==0) break;
        if ((errno!=EBUSY && errno!=EAGAIN) || attempt>=299)
            die("HCI_USER bind");
        if (attempt%10==0) fprintf(stderr,"HCI_USER waiting for kernel setup (%u/300)\n",attempt+1);
        sleep(1);
    }
    int master=posix_openpt(O_RDWR|O_NOCTTY|O_CLOEXEC);
    if (master<0 || grantpt(master)<0 || unlockpt(master)<0) die("PTY create");
    char *name=ptsname(master);
    if (!name) die("ptsname");
    int slave=open(name,O_RDWR|O_NOCTTY|O_CLOEXEC);
    if (slave<0) die("PTY slave");
    struct termios term;
    if (tcgetattr(slave,&term)<0) die("tcgetattr");
    cfmakeraw(&term);
    if (tcsetattr(slave,TCSANOW,&term)<0) die("tcsetattr");
    if (fchown(slave,1002,1002)<0 || fchmod(slave,0660)<0) die("PTY permissions");
    struct stat old;
    if (lstat(argv[2],&old)==0) {
        if (!S_ISLNK(old.st_mode)) { fprintf(stderr,"Refusing non-symlink %s\n",argv[2]); return 1; }
        if (unlink(argv[2])<0) die("remove previous PTY link");
    } else if (errno!=ENOENT) die("stat PTY link");
    if (symlink(name,argv[2])<0) die("PTY link");
    signal(SIGTERM,stop); signal(SIGINT,stop); signal(SIGPIPE,SIG_IGN);
    printf("SQ2_HCI_BRIDGE_READY hci%ld %s -> %s\n",dev,argv[2],name);
    unsigned char packet[65540], stream[131080];
    size_t used=0; unsigned long tx=0,rx=0;
    struct pollfd fds[2]={{hci,POLLIN,0},{master,POLLIN,0}};
    while (!stopping) {
        int ready=poll(fds,2,1000);
        if (ready<0) { if(errno==EINTR) continue; die("poll"); }
        if (fds[0].revents & (POLLERR|POLLHUP|POLLNVAL)) break;
        if (fds[0].revents&POLLIN) {
            ssize_t n=recv(hci,packet,sizeof(packet),MSG_TRUNC);
            if (n<=0 || (size_t)n>sizeof(packet)) die("HCI receive");
            if (allwrite(master,packet,(size_t)n)<0) die("PTY send");
            rx++;
        }
        if (fds[1].revents&POLLIN) {
            ssize_t n=read(master,stream+used,sizeof(stream)-used);
            if (n<=0) die("PTY receive");
            used+=(size_t)n;
            while (used) {
                size_t header,body;
                switch(stream[0]) {
                case 1: header=4; break; /* command */
                case 2: header=5; break; /* ACL */
                case 3: header=4; break; /* SCO */
                case 5: header=5; break; /* ISO */
                default: fprintf(stderr,"Invalid outbound H4 type %u\n",stream[0]); return 1;
                }
                if (used<header) break;
                body=(header==4)?stream[3]:((unsigned)stream[3]|((unsigned)stream[4]<<8));
                if (stream[0]==5) body&=0x3fff;
                size_t total=header+body;
                if (used<total) break;
                ssize_t sent;
                do { sent=send(hci,stream,total,0); } while(sent<0 && errno==EINTR && !stopping);
                if (sent!=(ssize_t)total) die("HCI send");
                used-=total; memmove(stream,stream+total,used); tx++;
            }
            if (used==sizeof(stream)) { fprintf(stderr,"H4 input overflow\n"); return 1; }
        }
    }
    printf("SQ2_HCI_BRIDGE_STOP tx=%lu rx=%lu\n",tx,rx);
    unlink(argv[2]); close(slave); close(master); close(hci);
    return stopping ? 0 : 1;
}
