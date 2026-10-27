#define _GNU_SOURCE
#include <sys/syscall.h>
#include <sys/reboot.h>
#include <linux/reboot.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--execute")) {
        sync();reboot(LINUX_REBOOT_CMD_KEXEC);perror("kexec reboot");return 1;
    }
    if(argc!=4){fprintf(stderr,"usage: kexec-loader Image initrd cmdline | --execute\n");return 2;}
    int kernel=open(argv[1],O_RDONLY),initrd=open(argv[2],O_RDONLY);
    if(kernel<0 || initrd<0){perror("open boot payload");return 1;}
    if(syscall(SYS_kexec_file_load,kernel,initrd,strlen(argv[3])+1,argv[3],0)<0){perror("kexec_file_load");return 1;}
    puts("KEXEC_IMAGE_LOADED (not executed)");return 0;
}
