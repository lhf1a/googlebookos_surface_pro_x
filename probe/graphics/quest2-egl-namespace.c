/* Isolate the private system-loader test before exposing any candidate files. */
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
int main(int argc,char **argv){
    if(argc>2||(argc==2&&strcmp(argv[1],"--ahb")))return 2;
    struct stat init_ns,self_ns;
    if(geteuid()!=0||stat("/proc/1/ns/mnt",&init_ns)||unshare(CLONE_NEWNS)||
       stat("/proc/self/ns/mnt",&self_ns)||self_ns.st_ino==init_ns.st_ino){
        perror("private EGL namespace");return 1;
    }
    if(mount(NULL,"/",NULL,MS_REC|MS_PRIVATE,NULL)||
       mount("tmpfs","/vendor/lib64/egl","tmpfs",MS_NOSUID|MS_NODEV,"size=268435456,mode=755")){
        perror("private EGL mount");return 1;
    }
    execl("/system/bin/sh","sh","/data/local/tmp/sq2-closed/run-quest2-gles-system.sh",
          "--inside",argc==2?"--ahb":NULL,(char*)NULL);
    perror("private EGL exec");return 1;
}
