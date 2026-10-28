/* Isolate the private system-loader test before exposing any candidate files. */
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
int main(int argc,char **argv){
    const char *modes[]={"--ahb","--window","--yuv","--visible","--readpixels","--composition","--combined","--fp16",
        "--vulkan","--vulkan-offscreen","--vulkan-properties","--vulkan-import",
        "--vulkan-window","--vulkan-window-create"};
    int valid=argc==1;
    if(argc==2)for(unsigned i=0;i<sizeof(modes)/sizeof(modes[0]);i++)if(!strcmp(argv[1],modes[i]))valid=1;
    if(!valid)return 2;
    struct stat init_ns,self_ns;
    if(geteuid()!=0||stat("/proc/1/ns/mnt",&init_ns)||unshare(CLONE_NEWNS)||
       stat("/proc/self/ns/mnt",&self_ns)||self_ns.st_ino==init_ns.st_ino){
        perror("private EGL namespace");return 1;
    }
    if(mount(NULL,"/",NULL,MS_REC|MS_PRIVATE,NULL)||
       mount("tmpfs","/vendor/lib64/egl","tmpfs",MS_NOSUID|MS_NODEV,"size=268435456,mode=755")){
        perror("private EGL mount");return 1;
    }
    const char *direct=getenv("SQ2_GLES_DIRECT");
    const char *readback=getenv("SQ2_GLES_GPU_READBACK");
    if(((direct&&!strcmp(direct,"1"))||(readback&&!strcmp(readback,"1")))&&
       mount("tmpfs","/data/vendor","tmpfs",MS_NOSUID|MS_NODEV,"size=1048576,mode=755")){
        perror("private ESX configuration mount");return 1;
    }
    execl("/system/bin/sh","sh","/data/local/tmp/sq2-closed/run-quest2-gles-system.sh",
          "--inside",argc==2?argv[1]:NULL,(char*)NULL);
    perror("private EGL exec");return 1;
}
