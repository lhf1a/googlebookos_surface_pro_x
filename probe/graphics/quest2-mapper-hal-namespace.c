/* Execute the standard mapper client in a private ODM HAL directory. */
#define _GNU_SOURCE
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/android/binderfs.h>
#include <sched.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
static int copy(const char *source,const char *target) {
    int in=open(source,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(in<0)return -1;
    int out=open(target,O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC,0644);
    if(out<0){close(in);return -1;}
    char bytes[65536];ssize_t n;int error=0;
    while((n=read(in,bytes,sizeof(bytes)))>0) {
        ssize_t done=0;
        while(done<n){ssize_t w=write(out,bytes+done,n-done);if(w<=0){error=1;break;}done+=w;}
        if(error)break;
    }
    if(n<0)error=1;
    if(close(out))error=1;
    close(in);return error?-1:0;
}
int main(int argc,char **argv) {
    if((argc!=3 && argc!=4 && argc!=5 && argc!=6) || argv[1][0]!='/' || argv[2][0]!='/' || geteuid()!=0)return 64;
    int mapper5=argc>=4 && !strcmp(argv[3],"--mapper5");
    if(mapper5 && (argc>5 || (argc==5 && strcmp(argv[4],"--egl"))))return 64;
    if(argc>=4 && !mapper5 && strcmp(argv[3],"--libui"))return 64;
    if(argc>=5 && !mapper5 && argv[4][0]!='/')return 64;
    if(argc==6 && strcmp(argv[5],"--egl"))return 64;
    struct stat before,after,init;
    if(stat("/proc/self/ns/mnt",&before) || unshare(CLONE_NEWNS) ||
       mount(NULL,"/",NULL,MS_REC|MS_PRIVATE,NULL) || stat("/proc/self/ns/mnt",&after) ||
       stat("/proc/1/ns/mnt",&init) || before.st_ino==after.st_ino || after.st_ino==init.st_ino)return 1;
    if(mount("tmpfs","/odm","tmpfs",MS_NOSUID|MS_NODEV,"size=134217728,mode=755") ||
       mkdir("/odm/lib64",0755) || mkdir("/odm/lib64/hw",0755)){perror("private ODM");return 2;}
    DIR *dir=opendir(argv[1]);if(!dir)return 3;
    struct dirent *entry;int result=0;
    while((entry=readdir(dir))) {
        const char *name=entry->d_name;size_t n=strlen(name);
        if(n<4 || strcmp(name+n-3,".so"))continue;
        if(!strcmp(name,"libquest2-native-mapper.so") ||
           !strcmp(name,"android.hardware.graphics.mapper@3.0-impl-qti-display.so"))continue;
        char source[PATH_MAX],target[PATH_MAX];
        if(snprintf(source,sizeof(source),"%s/%s",argv[1],name)>=(int)sizeof(source) ||
           snprintf(target,sizeof(target),"/odm/lib64/%s%s",
             (!strcmp(name,"android.hardware.graphics.mapper@3.0-impl-quest2.so") ||
              !strcmp(name,"android.hardware.graphics.mapper@4.0-impl-quest2.so"))?"hw/":"",name)>=(int)sizeof(target) ||
           copy(source,target)){result=4;break;}
    }
    closedir(dir);if(result)return result;
    /* Hide newer mapper implementations only inside this child's namespace.
       libui then exercises its normal fallback order without symbol interposition. */
    if(argc>=4 && mount("tmpfs","/vendor/lib64/hw","tmpfs",
                        MS_NOSUID|MS_NODEV,"size=4096,mode=755")) {
        perror("private vendor HAL mask");return 7;
    }
    if(argc>=5 && !mapper5) {
        struct stat library;
        if(lstat(argv[4],&library) || !S_ISREG(library.st_mode) ||
           mount(argv[4],"/system/lib64/libui.so",NULL,MS_BIND,NULL)) {
            perror("private libui compatibility candidate");return 8;
        }
        puts("PRIVATE_LIBUI_LEGACY_FALLBACK enabled=1 device_api_unchanged=1");
    }
    if(argc==6 || (mapper5 && argc==5)) {
        /* libhardware requires the resolved HAL path to stay inside hw/. */
        if(mapper5 && (rename("/odm/lib64/gralloc.kona.so","/odm/lib64/hw/gralloc.kona.so") ||
                       symlink("hw/gralloc.kona.so","/odm/lib64/gralloc.kona.so") ||
                       symlink("gralloc.kona.so","/odm/lib64/hw/gralloc.default.so")))return 9;
        if(mount("tmpfs","/vendor/lib64/egl","tmpfs",MS_NOSUID|MS_NODEV,
                 "size=1048576,mode=755"))return 9;
        const char *libs[]={"libEGL_adreno.so","libGLESv2_adreno.so","libgsl.so",
            "libadreno_utils.so","eglSubDriverAndroid.so","libq2v77glnext.so","libq2v77qgl.so"};
        for(size_t i=0;i<sizeof(libs)/sizeof(libs[0]);i++) {
            char source[PATH_MAX],target[PATH_MAX];
            if(snprintf(source,sizeof(source),"/odm/lib64/%s",libs[i])>=(int)sizeof(source) ||
               snprintf(target,sizeof(target),"/vendor/lib64/egl/%s",libs[i])>=(int)sizeof(target) ||
               symlink(source,target))return 9;
        }
        if(symlink("libEGL_adreno.so","/vendor/lib64/egl/libEGL_freedreno.so") ||
           symlink("libGLESv2_adreno.so","/vendor/lib64/egl/libGLESv2_freedreno.so"))return 9;
        char config[PATH_MAX];
        if(snprintf(config,sizeof(config),"%s/qgl_config.txt",argv[1])>=(int)sizeof(config) ||
           copy(config,"/vendor/lib64/egl/qgl_config.txt"))return 9;
        puts("PRIVATE_STANDARD_EGL native_mapper_hal=1");
    }
    if(mapper5) {
        char manifest[PATH_MAX];
        if(mkdir("/odm/etc",0755) || mkdir("/odm/etc/vintf",0755) ||
           mkdir("/odm/etc/vintf/manifest",0755) ||
           snprintf(manifest,sizeof(manifest),"%s/mapper.quest2.xml",argv[1])>=(int)sizeof(manifest) ||
           copy(manifest,"/odm/etc/vintf/manifest/mapper.quest2.xml"))return 10;
        struct stat original,current,permissions[3];
        const char *names[]={"binder","hwbinder","vndbinder"};
        for(unsigned i=0;i<3;i++) {
            char path[64];snprintf(path,sizeof(path),"/dev/%s",names[i]);
            if(stat(path,&permissions[i]) || !S_ISCHR(permissions[i].st_mode))return 10;
        }
        if(stat("/dev/binder",&original) ||
           mount("binder","/dev/binderfs","binder",MS_NOSUID|MS_NOEXEC,NULL)) {
            perror("private binderfs");return 10;
        }
        int ctl=open("/dev/binderfs/binder-control",O_RDONLY|O_CLOEXEC);
        if(ctl<0)return 10;
        for(unsigned i=0;i<3;i++) {
            struct binderfs_device device={0};strcpy(device.name,names[i]);
            if(ioctl(ctl,BINDER_CTL_ADD,&device) && errno!=EEXIST) {
                perror("private binder device");close(ctl);return 10;
            }
            /* Real services retain their init UID; binderfs defaults to root-only. */
            char path[64];snprintf(path,sizeof(path),"/dev/binderfs/%s",names[i]);
            if(chown(path,permissions[i].st_uid,permissions[i].st_gid) ||
               chmod(path,permissions[i].st_mode&0777)) {
                perror("private binder permissions");close(ctl);return 10;
            }
        }
        close(ctl);
        if(stat("/dev/binder",&current) || original.st_rdev==current.st_rdev) {
            fputs("binder context not isolated\n",stderr);return 10;
        }
        printf("PRIVATE_BINDER_CONTEXT old_device=%llu new_device=%llu\n",
               (unsigned long long)original.st_rdev,(unsigned long long)current.st_rdev);
    }
    if(unsetenv("LD_PRELOAD") || unsetenv("LD_LIBRARY_PATH"))return 5;
    printf("PRIVATE_MAPPER_HAL_NAMESPACE current=%llu init=%llu\n",
           (unsigned long long)after.st_ino,(unsigned long long)init.st_ino);fflush(stdout);
    if(mapper5)execl("/system/bin/sh","sh",argv[2],argv[1],argc==5?"--egl":NULL,NULL);
    else execl(argv[2],argv[2],NULL);
    perror("exec mapper client");return 6;
}
