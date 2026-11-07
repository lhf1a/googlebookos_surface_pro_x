/* Process-local observation. Only opt-in /dev/video32 redirection; no ioctl emulation. */
#define _GNU_SOURCE
#define BIONIC_IOCTL_NO_SIGNEDNESS_OVERLOAD
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#ifdef __ANDROID__
typedef int request_t;
#else
typedef unsigned long request_t;
#endif
static int (*next_open)(const char *,int,...);
static int (*next_open64)(const char *,int,...);
static int (*next_open2)(const char *,int);
static int (*next_openat)(int,const char *,int,...);
static int (*next_openat64)(int,const char *,int,...);
static int (*next_openat2)(int,const char *,int);
static int (*next_ioctl)(int,request_t,...);
static char video_node[PATH_MAX];
static __thread int tracing;
static void trace(const char *format, ...) {
    int saved=errno;
    if (!tracing) {
        tracing=1; char line[4096];
        va_list args; va_start(args,format); int count=vsnprintf(line,sizeof(line),format,args); va_end(args);
        if (count>0) {
            size_t length=(size_t)count<sizeof(line)?(size_t)count:sizeof(line)-1;
            (void)syscall(SYS_write,STDERR_FILENO,line,length);
        }
        tracing=0;
    }
    errno=saved;
}
__attribute__((constructor)) static void initialize(void) {
    int saved=errno;
    next_open=dlsym(RTLD_NEXT,"open"); next_open64=dlsym(RTLD_NEXT,"open64");
    next_open2=dlsym(RTLD_NEXT,"__open_2"); next_openat=dlsym(RTLD_NEXT,"openat");
    next_openat64=dlsym(RTLD_NEXT,"openat64"); next_openat2=dlsym(RTLD_NEXT,"__openat_2");
    next_ioctl=dlsym(RTLD_NEXT,"ioctl");
    const char *target=getenv("QUEST2_VIDEO_NODE");
    if (target && !strncmp(target,"/dev/video",10) && target[10]) {
        const char *p=target+10; while (*p>='0' && *p<='9') ++p;
        if (!*p && strlen(target)<sizeof(video_node)) strcpy(video_node,target);
    }
    trace("Q2TRACE initialized map_video32=%s ioctl_emulation=0\n",video_node[0]?video_node:"disabled");
    if (target && !video_node[0]) trace("Q2TRACE invalid QUEST2_VIDEO_NODE ignored; original paths retained\n");
    errno=saved;
}
static const char *mapped(const char *path) {
    return path && video_node[0] && !strcmp(path,"/dev/video32") ? video_node : path;
}
static int needs_mode(int flags) {
    return (flags&O_CREAT) || ((flags&O_TMPFILE)==O_TMPFILE);
}
static int observed_open(const char *api,int dir,const char *path,int flags,mode_t mode,int fortified) {
    const char *actual=mapped(path); int result;
    trace("Q2TRACE %s BEGIN dirfd=%d path=%s effective=%s flags=0x%x mode=0%o\n",api,dir,path?path:"(null)",actual?actual:"(null)",flags,(unsigned)mode);
    if (!strcmp(api,"open") && next_open) result=next_open(actual,flags,mode);
    else if (!strcmp(api,"open64") && next_open64) result=next_open64(actual,flags,mode);
    else if (!strcmp(api,"__open_2") && next_open2) result=next_open2(actual,flags);
    else if (!strcmp(api,"openat") && next_openat) result=next_openat(dir,actual,flags,mode);
    else if (!strcmp(api,"openat64") && next_openat64) result=next_openat64(dir,actual,flags,mode);
    else if (!strcmp(api,"__openat_2") && next_openat2) result=next_openat2(dir,actual,flags);
    else {
        if (fortified && needs_mode(flags)) abort(); /* retain missing-mode fortify failure */
        result=(int)syscall(SYS_openat,dir,actual,flags,mode);
    }
    int saved=errno;
    trace("Q2TRACE %s END return=%d errno=%d path=%s effective=%s\n",api,result,saved,path?path:"(null)",actual?actual:"(null)");
    errno=saved; return result;
}
#define OPEN_WRAPPER(name) \
int name(const char *path,int flags,...) { \
    mode_t mode=0; if(needs_mode(flags)){va_list args;va_start(args,flags);mode=va_arg(args,unsigned int);va_end(args);} \
    return observed_open(#name,AT_FDCWD,path,flags,mode,0); }
OPEN_WRAPPER(open)
OPEN_WRAPPER(open64)
#define OPENAT_WRAPPER(name) \
int name(int dir,const char *path,int flags,...) { \
    mode_t mode=0; if(needs_mode(flags)){va_list args;va_start(args,flags);mode=va_arg(args,unsigned int);va_end(args);} \
    return observed_open(#name,dir,path,flags,mode,0); }
OPENAT_WRAPPER(openat)
OPENAT_WRAPPER(openat64)
int __open_2(const char *path,int flags) {return observed_open("__open_2",AT_FDCWD,path,flags,0,1);}
int __openat_2(int dir,const char *path,int flags) {return observed_open("__openat_2",dir,path,flags,0,1);}
/* A tail branch preserves the raw third argument register, including ioctl(fd,op)
 * calls whose opcode needs no argument. No undefined va_arg read or fake value. */
__attribute__((visibility("hidden"))) int quest2_ioctl_forward(int fd,request_t request,uintptr_t argument) {
    int saved=errno; char fdpath[64],target[PATH_MAX];
    snprintf(fdpath,sizeof(fdpath),"/proc/self/fd/%d",fd);
    ssize_t n=readlink(fdpath,target,sizeof(target)-1);
    if(n<0) strcpy(target,"unresolved"); else target[n]=0;
    errno=saved;
    trace("Q2TRACE ioctl BEGIN fd=%d path=%s request=0x%08x arg=0x%lx\n",fd,target,(unsigned)request,(unsigned long)argument);
    int result=next_ioctl?next_ioctl(fd,request,argument):(int)syscall(SYS_ioctl,fd,(unsigned)request,argument);
    saved=errno;
    trace("Q2TRACE ioctl END fd=%d path=%s request=0x%08x return=%d errno=%d\n",fd,target,(unsigned)request,result,saved);
    errno=saved;return result;
}
#if defined(__aarch64__)
__asm__(".text\n.globl ioctl\n.type ioctl,%function\nioctl:\nb quest2_ioctl_forward\n.size ioctl,.-ioctl\n");
#elif defined(__x86_64__)
__asm__(".text\n.globl ioctl\n.type ioctl,@function\nioctl:\njmp quest2_ioctl_forward\n.size ioctl,.-ioctl\n");
#else
#error raw ioctl trampoline only supports AArch64 target and x86_64 host fixture
#endif