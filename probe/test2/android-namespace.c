#define _GNU_SOURCE
#include <sched.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
static void fail(const char *s){perror(s);exit(1);}
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    FILE *guard=fopen("/run/android-device-cgroup/test/cgroup.procs","w");
    if(!guard)fail("device cgroup guard");
    if(fprintf(guard,"%d\n",getpid())<0 || fclose(guard))fail("join device guard");
    const char *disks[]={"/dev/nvme0n1","/dev/sda","/dev/vda"};
    for(unsigned i=0;i<sizeof(disks)/sizeof(disks[0]);i++) {
        if(access(disks[i],F_OK))continue;
        int fd=open(disks[i],O_RDONLY);
        if(fd>=0){close(fd);fprintf(stderr,"Raw disk guard failed: %s\n",disks[i]);return 1;}
        if(errno!=EPERM && errno!=EACCES)fail("unexpected raw disk guard result");
        fprintf(stderr,"RAW_BLOCK_ACCESS_DENIED=%s\n",disks[i]);
    }
    if(unshare(CLONE_NEWNS|CLONE_NEWPID|CLONE_NEWIPC|CLONE_NEWUTS|CLONE_NEWNET)<0)fail("unshare");
    if(mount(NULL,"/",NULL,MS_REC|MS_PRIVATE,NULL)<0)fail("private mounts");
    pid_t pid=fork();if(pid<0)fail("fork");
    if(pid==0) {
        if(prctl(PR_SET_PDEATHSIG,SIGKILL)<0)fail("pdeathsig");
        if(getppid()==1) return 1;
        if(sethostname("sq2-googlebook-test",19)<0)fail("hostname");
        if(chdir(argv[1])<0 || chroot(".")<0 || chdir("/")<0)fail("chroot");
        execl("/debug_ramdisk/busybox","busybox","sh","/debug_ramdisk/android-inner",NULL);
        fail("exec");
    }
    int status;if(waitpid(pid,&status,0)<0)fail("wait");
    if(WIFEXITED(status))return WEXITSTATUS(status);
    fprintf(stderr,"ANDROID_NAMESPACE_SIGNAL=%d\n",WTERMSIG(status));return 128+WTERMSIG(status);
}
