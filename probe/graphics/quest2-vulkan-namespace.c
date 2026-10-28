/* Vulkan-only private namespace. Keep an fd for the original Mesa EGL tree. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
int main(int argc,char **argv) {
    if(argc!=2 || (strcmp(argv[1],"--device") && strcmp(argv[1],"--offscreen") &&
       strcmp(argv[1],"--ahb") && strcmp(argv[1],"--window")))return 2;
    struct stat before,after;
    if(geteuid()!=0 || stat("/proc/1/ns/mnt",&before) || unshare(CLONE_NEWNS) ||
       stat("/proc/self/ns/mnt",&after) || before.st_ino==after.st_ino ||
       mount(NULL,"/",NULL,MS_REC|MS_PRIVATE,NULL))return 1;
    int fd=open("/vendor/lib64/egl",O_RDONLY|O_DIRECTORY);
    if(fd<0 || dup2(fd,9)<0)return 1;
    if(fd!=9)close(fd);
    if(mount("tmpfs","/vendor/lib64/egl","tmpfs",MS_NOSUID|MS_NODEV,"size=268435456,mode=755"))return 1;
    execl("/system/bin/sh","sh","/data/local/tmp/sq2-closed/run-quest2-vulkan-only.sh",
          "--inside",argv[1],(char*)NULL);
    perror("Vulkan-only exec");return 1;
}
