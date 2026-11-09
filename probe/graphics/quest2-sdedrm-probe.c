/* Read-only initialization of the genuine QTI SDE DRM backend on the discovered MSM card.
 * Uses only the public C factory ABI; does not assume private C++ object/structure layouts. */
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <signal.h>
#include <sys/ucontext.h>
#include <stdint.h>

static uintptr_t backend_base;
static void crash_field(const char *label,size_t length,uint64_t value) {
    char hex[17];const char digits[]="0123456789abcdef";
    for(unsigned i=0;i<16;i++)hex[15-i]=digits[(value>>(i*4))&15];
    hex[16]='\n';write(STDERR_FILENO,label,length);write(STDERR_FILENO,hex,sizeof(hex));
}
static void crash_handler(int sig,siginfo_t *info,void *context) {
    const ucontext_t *u=context;
#define FIELD(label,value) crash_field(label,sizeof(label)-1,(uint64_t)(value))
    FIELD("QTI_DRM_SIGNAL=",sig);FIELD("fault=",info->si_addr);
    FIELD("pc=",u->uc_mcontext.pc);FIELD("pc_minus_backend=",u->uc_mcontext.pc-backend_base);
    FIELD("lr=",u->uc_mcontext.regs[30]);FIELD("lr_minus_backend=",u->uc_mcontext.regs[30]-backend_base);
    FIELD("x0=",u->uc_mcontext.regs[0]);FIELD("x1=",u->uc_mcontext.regs[1]);
    FIELD("x2=",u->uc_mcontext.regs[2]);FIELD("x3=",u->uc_mcontext.regs[3]);
#undef FIELD
    _exit(128+sig);
}
static unsigned properties,blobs,resources;
static void *next_symbol(const char *name) {
    void *symbol=dlsym(RTLD_NEXT,name);
    if(!symbol){fprintf(stderr,"QTI_DRM_MISSING_SYMBOL %s: %s\n",name,dlerror());exit(2);}
    return symbol;
}
drmModePropertyPtr drmModeGetProperty(int fd,uint32_t id) {
    drmModePropertyPtr (*next)(int,uint32_t)=next_symbol("drmModeGetProperty");
    drmModePropertyPtr p=next(fd,id);properties++;
    if(p)printf("QTI_DRM_PROPERTY id=%u name=%s flags=%x\n",p->prop_id,p->name,p->flags);
    else printf("QTI_DRM_PROPERTY_FAILED id=%u\n",id);
    return p;
}
drmModePropertyBlobPtr drmModeGetPropertyBlob(int fd,uint32_t id) {
    drmModePropertyBlobPtr (*next)(int,uint32_t)=next_symbol("drmModeGetPropertyBlob");
    drmModePropertyBlobPtr p=next(fd,id);blobs++;
    printf("QTI_DRM_BLOB id=%u bytes=%u found=%d\n",id,p?p->length:0,p!=NULL);
    return p;
}
drmModeResPtr drmModeGetResources(int fd) {
    drmModeResPtr (*next)(int)=next_symbol("drmModeGetResources");
    drmModeResPtr p=next(fd);resources++;
    if(p)printf("QTI_DRM_RESOURCES crtcs=%d connectors=%d encoders=%d\n",
                p->count_crtcs,p->count_connectors,p->count_encoders);
    return p;
}
/* The factory is only being inspected; unexpected display mutation stops this probe. */
int drmSetMaster(int fd) {
    (void)fd;fputs("QTI_DRM_UNEXPECTED_SETMASTER\n",stderr);abort();
}
int drmModeAtomicCommit(int fd,drmModeAtomicReqPtr req,uint32_t flags,void *data) {
    (void)fd;(void)req;(void)flags;(void)data;
    fputs("QTI_DRM_UNEXPECTED_COMMIT\n",stderr);abort();
}
static int open_display(void) {
    DIR *directory=opendir("/dev/dri");assert(directory);
    int result=-1;struct dirent *entry;
    while((entry=readdir(directory))) {
        unsigned index;char tail;
        if(sscanf(entry->d_name,"card%u%c",&index,&tail)!=1)continue;
        char path[256];int n=snprintf(path,sizeof(path),"/dev/dri/%s",entry->d_name);
        assert(n>0 && (size_t)n<sizeof(path));
        int fd=open(path,O_RDWR|O_CLOEXEC);if(fd<0)continue;
        drmVersionPtr version=drmGetVersion(fd);
        if(version && version->name && !strcmp(version->name,"msm")) {
            assert(result==-1);result=fd;
            printf("QTI_DRM_DEVICE path=%s driver=%s master=%d\n",path,version->name,drmIsMaster(fd));
            assert(!drmIsMaster(fd));
        } else close(fd);
        drmFreeVersion(version);
    }
    closedir(directory);assert(result>=0);return result;
}
int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);
    void *library=dlopen("libsdedrm.so",RTLD_NOW|RTLD_LOCAL);
    if(!library){fprintf(stderr,"QTI_DRM_DLOPEN_FAILED %s\n",dlerror());return 2;}
    /* sm8150 drm_interface.h factory ABI, independently checked against donor disassembly. */
    int (*get_manager)(int,void **)=dlsym(library,"GetDRMManager");
    int (*destroy_manager)(void)=dlsym(library,"DestroyDRMManager");
    assert(get_manager && destroy_manager);
    Dl_info origin;assert(dladdr((void *)get_manager,&origin));
    backend_base=(uintptr_t)origin.dli_fbase;
    printf("QTI_DRM_BACKEND path=%s base=%p pid=%d\n",origin.dli_fname,origin.dli_fbase,getpid());
    struct sigaction action={0};action.sa_sigaction=crash_handler;action.sa_flags=SA_SIGINFO;
    sigemptyset(&action.sa_mask);assert(!sigaction(SIGSEGV,&action,NULL));
    assert(!sigaction(SIGABRT,&action,NULL));
    int fd=open_display();void *manager=NULL;
    int status=get_manager(fd,&manager);
    printf("QTI_DRM_INIT status=%d manager_present=%d resources=%u properties=%u blobs=%u\n",
           status,manager!=NULL,resources,properties,blobs);
    int destroyed=destroy_manager();
    printf("QTI_DRM_DESTROY status=%d master=%d\n",destroyed,drmIsMaster(fd));
    assert(!drmIsMaster(fd));close(fd);
    printf("QTI_DRM_RESULT init=%d destroyed=%d atomic_commit=0 physical_display_verified=0\n",status,destroyed);
    return status || destroyed ? 1:0;
}
