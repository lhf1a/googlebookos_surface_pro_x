#define _GNU_SOURCE
#include <sched.h>
#include <sys/mount.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(int argc,char** argv){
 if(argc!=2||(strcmp(argv[1],"0")&&strcmp(argv[1],"1")&&strcmp(argv[1],"--serve")))return 2;
 if(unshare(CLONE_NEWNS)||mount(NULL,"/",NULL,MS_REC|MS_PRIVATE,NULL)) {perror("private namespace");return 3;}
 if(mount("/data/local/tmp/sq2-map-cache/libminigbm_gralloc.so","/vendor/lib64/libminigbm_gralloc.so",NULL,MS_BIND,NULL)){perror("private bind");return 4;}
 setenv("LD_LIBRARY_PATH","/system/lib64:/apex/com.android.media.swcodec/lib64:/vendor/lib64",1);
 int serve=!strcmp(argv[1],"--serve");
 setenv("SQ2_VIDEO_MAP_CACHE",serve?"1":argv[1],1);unsetenv("LD_PRELOAD");
 if(serve){execl("/data/local/tmp/sq2-c2-owner","sq2-c2-owner","--serve",NULL);perror("exec service");return 6;}
 execl("/data/local/tmp/memory-probe","memory-probe",NULL);perror("exec probe");return 5;
}
