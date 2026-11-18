/* SPDX-License-Identifier: MIT */
/* Experimental QTI/GSL import bridge. Not enabled in normal boot images.
 * Native assays use explicit environment opt-in. An experimental boot may
 * select one ordinary Android application via debug.sq2.vmbind.app, before
 * starting a fresh process. No global libc symbol interposition.
 */
#define _GNU_SOURCE
#include "qti-msm-vmbind-registry.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/dma-buf.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#ifdef __ANDROID__
#include <sys/system_properties.h>
#include <android/log.h>
#endif

#define EXPORT __attribute__((visibility("default")))
struct bridge_queue { uint32_t visible, native; struct bridge_queue *next; };
struct bridge_context {
    struct qti_vm vm;
    struct qti_vm_registry registry;
    pthread_mutex_t lock;
    unsigned aliases, users;
    uint64_t submits, bo_entries, allocations, imports;
    uint32_t next_visible_queue;
    bool exclude_policy;
#ifdef __ANDROID__
    bool sample_game;
    uint64_t explicit_private_new, prime_exports, cpu_prep_calls, flink_calls;
#endif
    struct bridge_queue *queues;
};
struct bridge_alias { int fd; struct bridge_context *context; struct bridge_alias *next; };
static pthread_mutex_t table_lock=PTHREAD_MUTEX_INITIALIZER;
static struct bridge_alias *aliases;

static int raw_ioctl(int fd,unsigned long request,void *arg)
{
    int ret=ioctl(fd,request,arg);return ret<0?-errno:ret;
}
static void dispose(struct bridge_context *c)
{
    fprintf(stderr,"QTI_VM_BIND_CONTEXT submits=%llu bo_entries_removed=%llu allocations=%llu imports=%llu lost=%d\n",
            (unsigned long long)c->submits,(unsigned long long)c->bo_entries,
            (unsigned long long)c->allocations,(unsigned long long)c->imports,c->vm.lost);
    close(c->vm.fd); /* anchor is the final intercepted OFD reference */
    qti_vm_registry_finish(&c->registry,true);
    while(c->queues){struct bridge_queue *q=c->queues;c->queues=q->next;free(q);}
    pthread_mutex_destroy(&c->lock);free(c);
}
static struct bridge_context *acquire(int fd)
{
    pthread_mutex_lock(&table_lock);
    struct bridge_context *c=NULL;
    for(struct bridge_alias *a=aliases;a;a=a->next)if(a->fd==fd){c=a->context;c->users++;break;}
    pthread_mutex_unlock(&table_lock);return c;
}
static void release(struct bridge_context *c)
{
    pthread_mutex_lock(&table_lock);
    c->users--;bool destroy=!c->users&&!c->aliases;
    pthread_mutex_unlock(&table_lock);if(destroy)dispose(c);
}
/* Called only for a newly allocated descriptor or by our close wrapper. */
static struct bridge_context *unlink_alias(int fd)
{
    pthread_mutex_lock(&table_lock);
    struct bridge_alias **at=&aliases;struct bridge_context *c=NULL;
    while(*at){
        if((*at)->fd==fd){struct bridge_alias *a=*at;*at=a->next;
            c=a->context;c->aliases--;c->users++;free(a);break;}
        at=&(*at)->next;
    }
    pthread_mutex_unlock(&table_lock);return c;
}
static int add_alias(struct bridge_context *c,int fd)
{
    struct bridge_alias *a=malloc(sizeof(*a));if(!a)return -ENOMEM;
    struct bridge_context *stale=unlink_alias(fd);if(stale)release(stale);
    *a=(struct bridge_alias){fd,c,NULL};
    pthread_mutex_lock(&table_lock);a->next=aliases;aliases=a;c->aliases++;
    pthread_mutex_unlock(&table_lock);return 0;
}
static bool is_msm(int fd)
{
    struct stat st;if(fstat(fd,&st)||!S_ISCHR(st.st_mode))return false;
    char link[128],subsystem[PATH_MAX];
    int n=snprintf(link,sizeof(link),"/sys/dev/char/%u:%u/subsystem",major(st.st_rdev),minor(st.st_rdev));
    if(n<0||(size_t)n>=sizeof(link))return false;
    ssize_t len=readlink(link,subsystem,sizeof(subsystem)-1);
    if(len<=0)return false;
    subsystem[len]=0;
    const char *base=strrchr(subsystem,'/');base=base?base+1:subsystem;
    if(strcmp(base,"drm"))return false;
    char name[32]={0};struct drm_version v={.name=name,.name_len=31};
    return !ioctl(fd,DRM_IOCTL_VERSION,&v)&&!strcmp(name,"msm");
}
EXPORT bool qti_vm_app_selected(unsigned uid,const char *process,const char *package)
{
    /* Android application class, not a fixed device/user/app identity. */
    unsigned appid=uid%100000;
    if(appid<10000||appid>=20000||!process||!package||!*package)return false;
    size_t n=strlen(package);
    for(size_t i=0;i<n;i++){
        unsigned char c=(unsigned char)package[i];
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='.'))return false;
    }
    return !strncmp(process,package,n)&&(process[n]=='\0'||process[n]==':');
}
static bool enabled(void)
{
    const char *a=getenv("QTI_VM_BIND_TEST"),*b=getenv("QTI_VM_BIND_EXCLUSIVE_TEST");
    if(a&&b&&!strcmp(a,"1")&&!strcmp(b,"1"))return true;
#ifdef __ANDROID__
    char package[PROP_VALUE_MAX]={0},process[256]={0};
    if(__system_property_get("debug.sq2.vmbind.app",package)<=0)return false;
    int fd=open("/proc/self/cmdline",O_RDONLY|O_CLOEXEC);if(fd<0)return false;
    ssize_t n=read(fd,process,sizeof(process)-1);close(fd);
    return n>0&&qti_vm_app_selected((unsigned)getuid(),process,package);
#else
    return false;
#endif
}

EXPORT int qti_vm_gsl_open(const char *path,int flags,...)
{
    mode_t mode=0;
    if((flags&O_CREAT) || ((flags&O_TMPFILE)==O_TMPFILE)){
        va_list ap;va_start(ap,flags);mode=(mode_t)va_arg(ap,int);va_end(ap);
    }
    int fd=open(path,flags,mode);if(fd<0)return fd;
    int saved=errno;
    struct bridge_context *stale=unlink_alias(fd);if(stale)release(stale);
    if(!is_msm(fd)||!enabled()){errno=saved;return fd;}
    struct bridge_context *c=calloc(1,sizeof(*c));if(!c){close(fd);errno=ENOMEM;return -1;}
    int anchor=fcntl(fd,F_DUPFD_CLOEXEC,0);
    if(anchor<0){saved=errno;free(c);close(fd);errno=saved;return -1;}
    int r=pthread_mutex_init(&c->lock,NULL);
    if(r){close(anchor);close(fd);free(c);errno=r;return -1;}
#ifdef __ANDROID__
    char sample[PROP_VALUE_MAX]={0};
    c->sample_game=__system_property_get("debug.sq2.vmbind.sample",sample)>0&&!strcmp(sample,"1");
#endif
#ifdef __ANDROID__
    char policy[PROP_VALUE_MAX]={0};
    c->exclude_policy=__system_property_get("debug.sq2.vmbind.noshare_mode",policy)>0 &&
                      !strcmp(policy,"exclude");
#endif
    c->next_visible_queue=1; /* Queue zero is the kernel's legacy default. */
    r=qti_vm_init(&c->vm,anchor,(uint64_t)sysconf(_SC_PAGESIZE),qti_vm_real_ioctl,NULL);
    if(!r)r=qti_vm_registry_init(&c->registry,&c->vm,&qti_vm_sync_real_ops,NULL,qti_vm_real_fence_status);
    if(!r)r=add_alias(c,fd);
    if(r){
        fprintf(stderr,"QTI_VM_BIND_OPEN error=%d enabled=%d\n",r,c->vm.enabled);
        close(fd);close(anchor);qti_vm_registry_finish(&c->registry,true);
        pthread_mutex_destroy(&c->lock);free(c);errno=-r;return -1;
    }
    fprintf(stderr,"QTI_VM_BIND_OPEN enabled=1 experimental=1 va_start=0x%llx va_size=0x%llx\n",
            (unsigned long long)c->vm.va_start,(unsigned long long)c->vm.va_size);
    errno=saved;return fd;
}
EXPORT int qti_vm_gsl_close(int fd)
{
    struct bridge_context *c=unlink_alias(fd);
    int ret=close(fd),saved=errno;
    if(c)release(c);
    errno=saved;return ret;
}
extern int __open_2(const char *,int);
EXPORT int qti_vm_gsl_open2(const char *path,int flags)
{
    /* Preserve libc's fortified invalid-mode behavior, not a guessed vararg. */
    if((flags&O_CREAT)||((flags&O_TMPFILE)==O_TMPFILE))return __open_2(path,flags);
    return qti_vm_gsl_open(path,flags);
}
EXPORT int qti_vm_gsl_dup(int fd)
{
    struct bridge_context *c=acquire(fd);
    int ret=dup(fd),saved=errno;
    if(ret>=0&&c){int r=add_alias(c,ret);if(r){close(ret);ret=-1;saved=-r;}}
    if(c)release(c);
    errno=saved;return ret;
}
EXPORT int qti_vm_gsl_fcntl(int fd,int command,...)
{
    long arg=0;
    enum {NONE,INTEGER,POINTER} type=INTEGER;
    switch(command){
    case F_GETFD:case F_GETFL:case F_GETOWN: type=NONE;break;
#ifdef F_GETSIG
    case F_GETSIG:type=NONE;break;
#endif
#ifdef F_GETLEASE
    case F_GETLEASE:type=NONE;break;
#endif
#ifdef F_GETPIPE_SZ
    case F_GETPIPE_SZ:type=NONE;break;
#endif
#ifdef F_GET_SEALS
    case F_GET_SEALS:type=NONE;break;
#endif
    case F_GETLK:case F_SETLK:case F_SETLKW:type=POINTER;break;
#ifdef F_OFD_GETLK
    case F_OFD_GETLK:case F_OFD_SETLK:case F_OFD_SETLKW:type=POINTER;break;
#endif
#ifdef F_GETOWN_EX
    case F_GETOWN_EX:case F_SETOWN_EX:type=POINTER;break;
#endif
#ifdef F_GET_RW_HINT
    case F_GET_RW_HINT:case F_SET_RW_HINT:case F_GET_FILE_RW_HINT:case F_SET_FILE_RW_HINT:type=POINTER;break;
#endif
    default:break;
    }
    if(type!=NONE){va_list ap;va_start(ap,command);arg=type==POINTER?(long)va_arg(ap,void *):va_arg(ap,int);va_end(ap);}
    struct bridge_context *c=acquire(fd);
    int ret=fcntl(fd,command,arg),saved=errno;
    if(ret>=0&&c&&(command==F_DUPFD||command==F_DUPFD_CLOEXEC)){
        int r=add_alias(c,ret);if(r){close(ret);ret=-1;saved=-r;}
    }
    if(c)release(c);
    errno=saved;return ret;
}

static int export_bo(struct bridge_context *c,uint32_t handle)
{
    struct drm_prime_handle p={.handle=handle,.flags=DRM_CLOEXEC|DRM_RDWR,.fd=-1};
    int ret=raw_ioctl(c->vm.fd,DRM_IOCTL_PRIME_HANDLE_TO_FD,&p);
    return ret?ret:p.fd;
}
static int native_queue(struct bridge_context *c,uint32_t visible,uint32_t *native)
{
    if(!visible){*native=0;return 0;}
    for(struct bridge_queue *q=c->queues;q;q=q->next)
        if(q->visible==visible){*native=q->native;return 0;}
    return -ENOENT;
}
/* One creation hint from the in-process GSL allocation adapter. Never infer
 * privacy from an application identity, GEM size, or missing export history. */
static _Thread_local uint64_t private_gem_size;
/* 0: no cooperating GSL allocator; 1: normal pure allocation; 2: shared veto. */
static _Thread_local unsigned gsl_alloc_scope;
EXPORT unsigned qti_vm_swap_gsl_scope(unsigned role);
EXPORT unsigned qti_vm_swap_gsl_scope(unsigned role){
    unsigned old=gsl_alloc_scope;gsl_alloc_scope=role;return old;
}
EXPORT uint64_t qti_vm_swap_private_gem_size(uint64_t bytes);
EXPORT uint64_t qti_vm_swap_private_gem_size(uint64_t bytes)
{
    uint64_t old=private_gem_size; private_gem_size=bytes; return old;
}

static int track_new(struct bridge_context *c,struct drm_msm_gem_new *n)
{
    uint64_t expected=qti_vm_swap_private_gem_size(0);
    int ret;
    uint32_t effective_flags=n->flags;
    if(gsl_alloc_scope!=2 && ((expected && n->size==expected) ||
       (c->exclude_policy && gsl_alloc_scope==1)) &&
       !(n->flags & ~(MSM_BO_GPU_READONLY | MSM_BO_CACHE_MASK))) {
        struct drm_msm_gem_new req=*n;
        req.flags|=MSM_BO_NO_SHARE; effective_flags=req.flags;
        ret=raw_ioctl(c->vm.fd,DRM_IOCTL_MSM_GEM_NEW,&req);
        n->handle=req.handle;
    } else {
        ret=raw_ioctl(c->vm.fd,DRM_IOCTL_MSM_GEM_NEW,n);
    }
    if(ret)return ret;
    struct qti_vm_mapping map;
    ret=qti_vm_registry_add(&c->registry,n->handle,n->size,-1,QTI_VM_HANDLE_NEW,&map);
    if(ret){struct drm_gem_close g={.handle=n->handle};raw_ioctl(c->vm.fd,DRM_IOCTL_GEM_CLOSE,&g);n->handle=0;}
    else {
        c->allocations++;
#ifdef __ANDROID__
        if(c->sample_game && (effective_flags & MSM_BO_NO_SHARE))c->explicit_private_new++;
#endif
    }
    return ret;
}
static int promote(struct bridge_context *c,uint32_t handle)
{
    int fd=qti_vm_registry_dma_buf(&c->registry,handle);
    if(fd>=0)return 0;
    if(fd!=-1)return fd;
    fd=export_bo(c,handle);if(fd<0)return fd;
    int ret=qti_vm_registry_attach_dma_buf(&c->registry,handle,fd);
    close(fd);return ret;
}
static int track_open_name(struct bridge_context *c,struct drm_gem_open *g)
{
    int ret=raw_ioctl(c->vm.fd,DRM_IOCTL_GEM_OPEN,g);if(ret)return ret;
    struct qti_vm_mapping map;
    if(!qti_vm_registry_resolve(&c->registry,g->handle,&map))return promote(c,g->handle);
    int fd=export_bo(c,g->handle);
    if(fd<0)ret=fd;
    else{ret=qti_vm_registry_add(&c->registry,g->handle,g->size,fd,QTI_VM_HANDLE_PRIME,&map);close(fd);}
    if(ret){struct drm_gem_close close_bo={.handle=g->handle};raw_ioctl(c->vm.fd,DRM_IOCTL_GEM_CLOSE,&close_bo);g->handle=0;}
    else c->imports++;
    return ret;
}
static int track_import(struct bridge_context *c,struct drm_prime_handle *p)
{
    int ret=raw_ioctl(c->vm.fd,DRM_IOCTL_PRIME_FD_TO_HANDLE,p);if(ret)return ret;
    struct qti_vm_mapping existing;
    if(!qti_vm_registry_resolve(&c->registry,p->handle,&existing))
        return qti_vm_registry_attach_dma_buf(&c->registry,p->handle,p->fd);
    /* dma-buf's documented SEEK_END/0 query returns its size. */
    off_t size=lseek(p->fd,0,SEEK_END);
    if(size<=0)ret=size<0?-errno:-EINVAL;
    else{
        struct qti_vm_mapping map;
        ret=qti_vm_registry_add(&c->registry,p->handle,(uint64_t)size,p->fd,QTI_VM_HANDLE_PRIME,&map);
    }
    if(ret){struct drm_gem_close g={.handle=p->handle};raw_ioctl(c->vm.fd,DRM_IOCTL_GEM_CLOSE,&g);p->handle=0;}
    else c->imports++;
    return ret;
}
static int convert_submit(struct bridge_context *c,unsigned long request,void *arg)
{
    size_t size=_IOC_SIZE(request);
    if(size<40||size>sizeof(struct drm_msm_gem_submit))return -EINVAL;
    struct drm_msm_gem_submit legacy={0};memcpy(&legacy,arg,size);
    struct qti_vm_sync_batch batch={0};struct qti_vm_submit converted={0};
    /* c->lock pins private dependencies and shared FDs through publication. */
    struct qti_vm_prepared_bo *prepared=NULL;
    if(legacy.nr_bos){
        if(SIZE_MAX/legacy.nr_bos<sizeof(*prepared))return -EOVERFLOW;
        prepared=malloc(legacy.nr_bos*sizeof(*prepared));
        if(!prepared)return -ENOMEM;
    }
    int ret=qti_vm_registry_prepare_sync_prepared(&c->registry,&legacy,&batch,prepared);
    if(ret)goto done_batch;
    struct drm_msm_gem_submit explicit_req;
    ret=qti_vm_sync_prepare_request(&batch,&legacy,&explicit_req);if(ret)goto done_batch;
    ret=native_queue(c,legacy.queueid,&explicit_req.queueid);if(ret)goto done_batch;
    ret=qti_vm_prepare_submit_prepared(&c->vm,&explicit_req,prepared,&converted);
    if(ret)goto done_batch;
    ret=raw_ioctl(c->vm.fd,DRM_IOCTL_MSM_GEM_SUBMIT,&converted.request);
    if(!ret){
        int completion=converted.request.fence_fd;
        if(completion<0){c->vm.lost=true;ret=-EPROTO;goto done_batch;}
        c->submits++;c->bo_entries+=legacy.nr_bos;
        if(c->submits<=3)fprintf(stderr,"QTI_VM_BIND_SUBMIT visible_queue=%u native_queue=%u fence=%u legacy_bos=%u kernel_bos=%u cmds=%u\n",
            legacy.queueid,converted.request.queueid,converted.request.fence,legacy.nr_bos,converted.request.nr_bos,converted.request.nr_cmds);
        if(c->submits<=3 && getenv("QTI_VM_BIND_CLASS_SAMPLE")){
            uint32_t shared=0;
            for(uint32_t i=0;i<legacy.nr_bos;i++)shared+=!prepared[i].access;
            fprintf(stderr,"QTI_VM_BIND_CLASS lookup=%u bridge_private=%u bridge_shared=%u\n",
                    legacy.nr_bos,legacy.nr_bos-shared,shared);
        }
        ret=qti_vm_registry_note_submit(&c->registry,converted.request.queueid,completion);
        if(!ret)ret=qti_vm_registry_note_access_prepared(&c->registry,&legacy,completion,prepared);
        if(!ret)ret=qti_vm_sync_publish(&batch,completion);
#ifdef __ANDROID__
        if(!ret&&c->sample_game&&(c->submits<=3 || (c->submits%256)==0)){
            unsigned shared=0;
            for(uint32_t i=0;i<legacy.nr_bos;i++)shared+=!prepared[i].access;
            __android_log_print(ANDROID_LOG_INFO,"QtiVmbindLab",
                "submit=%llu table=%u bridge_shared=%u live=%zu alloc=%llu explicit_private_new=%llu imports_or_name_open=%llu prime_export_calls=%llu flink_calls=%llu cpu_prep_calls=%llu cache_full=%llu cache_saved=%llu template_hits=%llu",
                (unsigned long long)c->submits,legacy.nr_bos,shared,c->registry.live_count,
                (unsigned long long)c->allocations,(unsigned long long)c->explicit_private_new,
                (unsigned long long)c->imports,(unsigned long long)c->prime_exports,
                (unsigned long long)c->flink_calls,(unsigned long long)c->cpu_prep_calls,(unsigned long long)c->registry.cache_full,(unsigned long long)c->registry.cache_saved,(unsigned long long)c->registry.template_hits);
        }
#endif
        if(ret){c->vm.lost=true;close(completion);}
        else{
            legacy.fence=converted.request.fence;
            if(legacy.flags&MSM_SUBMIT_FENCE_FD_OUT)legacy.fence_fd=completion;
            else close(completion);
            memcpy(arg,&legacy,size);
        }
    }
done_batch:
    free(prepared);qti_vm_submit_finish(&converted);qti_vm_sync_finish(&batch);
    return ret;
}
static int dispatch(struct bridge_context *c,unsigned long request,void *arg)
{
    if(!arg)return -EFAULT;
    if(_IOC_TYPE(request)!='d')return raw_ioctl(c->vm.fd,request,arg);
    unsigned nr=_IOC_NR(request);
    /* Cleanup remains possible after a post-submit or asynchronous failure. */
    if(nr==_IOC_NR(DRM_IOCTL_GEM_CLOSE)){
        if(_IOC_SIZE(request)!=sizeof(struct drm_gem_close))return -EINVAL;
        struct drm_gem_close *g=arg;
        int ret=c->vm.lost?0:qti_vm_registry_retire(&c->registry,g->handle);
        if(ret)return ret;
        ret=raw_ioctl(c->vm.fd,request,arg);if(ret)c->vm.lost=true;return ret;
    }
    if(c->vm.lost)return -EPIPE;
    int ret=qti_vm_registry_collect(&c->registry);if(ret)return ret;
    if(nr==_IOC_NR(DRM_IOCTL_MSM_GEM_NEW)){
        if(_IOC_SIZE(request)!=sizeof(struct drm_msm_gem_new))return -EINVAL;
        return track_new(c,arg);
    }
    if(nr==_IOC_NR(DRM_IOCTL_PRIME_FD_TO_HANDLE)){
        if(_IOC_SIZE(request)!=sizeof(struct drm_prime_handle))return -EINVAL;
        return track_import(c,arg);
    }
    if(nr==_IOC_NR(DRM_IOCTL_PRIME_HANDLE_TO_FD)){
        if(_IOC_SIZE(request)!=sizeof(struct drm_prime_handle))return -EINVAL;
        ret=raw_ioctl(c->vm.fd,request,arg);if(ret)return ret;
        struct drm_prime_handle *p=arg;
        ret=qti_vm_registry_attach_dma_buf(&c->registry,p->handle,p->fd);
        if(ret){close(p->fd);p->fd=-1;}
#ifdef __ANDROID__
        else if(c->sample_game)c->prime_exports++;
#endif
        return ret;
    }
    if(nr==_IOC_NR(DRM_IOCTL_GEM_FLINK)){
        if(_IOC_SIZE(request)!=sizeof(struct drm_gem_flink))return -EINVAL;
#ifdef __ANDROID__
        if(c->sample_game)c->flink_calls++;
#endif
        ret=promote(c,((struct drm_gem_flink *)arg)->handle);if(ret)return ret;
        return raw_ioctl(c->vm.fd,request,arg);
    }
    if(nr==_IOC_NR(DRM_IOCTL_GEM_OPEN)){
        if(_IOC_SIZE(request)!=sizeof(struct drm_gem_open))return -EINVAL;
        return track_open_name(c,arg);
    }
    if(nr==_IOC_NR(DRM_IOCTL_MSM_GEM_INFO)){
        size_t size=_IOC_SIZE(request);
        if(size<16||size>sizeof(struct drm_msm_gem_info))return -EINVAL;
        struct drm_msm_gem_info info={0};memcpy(&info,arg,size);
        if(info.info==MSM_INFO_GET_IOVA||info.info==MSM_INFO_SET_IOVA){
            if(info.len||info.pad)return -EINVAL;
            struct qti_vm_mapping map;
            ret=qti_vm_registry_resolve(&c->registry,info.handle,&map);if(ret)return ret;
            if(info.info==MSM_INFO_SET_IOVA&&info.value!=map.iova)return -EOPNOTSUPP;
            info.value=map.iova;memcpy(arg,&info,size);return 0;
        }
    }
    if(nr==_IOC_NR(DRM_IOCTL_MSM_GEM_SUBMIT))return convert_submit(c,request,arg);
    if(nr==_IOC_NR(DRM_IOCTL_MSM_SUBMITQUEUE_NEW)){
        if(_IOC_SIZE(request)!=sizeof(struct drm_msm_submitqueue))return -EINVAL;
        if(!c->next_visible_queue)return -ENOSPC;
        struct bridge_queue *alias=calloc(1,sizeof(*alias));if(!alias)return -ENOMEM;
        struct drm_msm_submitqueue *q=arg;
        ret=raw_ioctl(c->vm.fd,request,arg);
        if(ret){free(alias);return ret;}
        alias->native=q->id;alias->visible=c->next_visible_queue++;
        alias->next=c->queues;c->queues=alias;q->id=alias->visible;
        fprintf(stderr,"QTI_VM_BIND_GFX_QUEUE visible=%u native=%u flags=0x%x\n",q->id,alias->native,q->flags);
        return 0;
    }
    if(nr==_IOC_NR(DRM_IOCTL_MSM_SUBMITQUEUE_CLOSE)){
        if(_IOC_SIZE(request)!=sizeof(uint32_t))return -EINVAL;
        uint32_t visible=*(uint32_t *)arg,native;
        ret=native_queue(c,visible,&native);if(ret)return ret;
        ret=raw_ioctl(c->vm.fd,request,&native);if(ret)return ret;
        qti_vm_registry_close_queue(&c->registry,native);
        struct bridge_queue **at=&c->queues;
        while(*at){if((*at)->visible==visible){struct bridge_queue *q=*at;*at=q->next;free(q);break;}at=&(*at)->next;}
        return 0;
    }
    if(nr==_IOC_NR(DRM_IOCTL_MSM_SUBMITQUEUE_QUERY)){
        if(_IOC_SIZE(request)!=sizeof(struct drm_msm_submitqueue_query))return -EINVAL;
        struct drm_msm_submitqueue_query q=*(struct drm_msm_submitqueue_query *)arg;
        uint32_t visible=q.id;ret=native_queue(c,visible,&q.id);if(ret)return ret;
        ret=raw_ioctl(c->vm.fd,request,&q);q.id=visible;
        if(!ret)*(struct drm_msm_submitqueue_query *)arg=q;
        return ret;
    }
    return raw_ioctl(c->vm.fd,request,arg);
}
static bool blocking_wait(unsigned long request)
{
    if(_IOC_TYPE(request)!='d')return false;
    unsigned nr=_IOC_NR(request);
    return nr==_IOC_NR(DRM_IOCTL_MSM_WAIT_FENCE)||
           nr==_IOC_NR(DRM_IOCTL_MSM_GEM_CPU_PREP)||
           nr==_IOC_NR(DRM_IOCTL_SYNCOBJ_WAIT)||
           nr==_IOC_NR(DRM_IOCTL_SYNCOBJ_TIMELINE_WAIT);
}
EXPORT int qti_vm_gsl_ioctl(int fd,unsigned long request,...)
{
    va_list ap;va_start(ap,request);void *arg=va_arg(ap,void *);va_end(ap);
    request=(unsigned int)request; /* bionic ioctl has a 32-bit request argument */
    struct bridge_context *c=acquire(fd);
    if(!c)return ioctl(fd,request,arg);
    pthread_mutex_lock(&c->lock);
    int ret;
    if(blocking_wait(request)){
        /* Snapshot bridge metadata, then let another CPU thread submit/signal.
         * acquire() pins the context and anchor FD until the wait returns,
         * even if the caller's alias is closed in the meantime. */
        struct drm_msm_wait_fence wait={0};
        ret=!arg?-EFAULT:c->vm.lost?-EPIPE:0;
        if(!ret&&_IOC_NR(request)==_IOC_NR(DRM_IOCTL_MSM_WAIT_FENCE)){
            size_t size=_IOC_SIZE(request);
            if(size<24||size>sizeof(wait))ret=-EINVAL;
            else{
                memcpy(&wait,arg,size);
                ret=native_queue(c,wait.queueid,&wait.queueid);
                request=DRM_IOCTL_MSM_WAIT_FENCE;arg=&wait;
            }
        }
        if(!ret&&_IOC_NR(request)==_IOC_NR(DRM_IOCTL_MSM_GEM_CPU_PREP)){
            if(_IOC_SIZE(request)!=sizeof(struct drm_msm_gem_cpu_prep))ret=-EINVAL;
            else {
#ifdef __ANDROID__
                if(c->sample_game)c->cpu_prep_calls++;
#endif
                ret=promote(c,((struct drm_msm_gem_cpu_prep *)arg)->handle);
            }
        }
        pthread_mutex_unlock(&c->lock);
        if(!ret)ret=raw_ioctl(c->vm.fd,request,arg);
        pthread_mutex_lock(&c->lock);
    }else ret=dispatch(c,request,arg);
    if(ret<0)fprintf(stderr,"QTI_VM_BIND_IOCTL nr=0x%x size=%u error=%d lost=%d\n",
                     (unsigned)_IOC_NR(request),(unsigned)_IOC_SIZE(request),ret,c->vm.lost);
    pthread_mutex_unlock(&c->lock);release(c);
    if(ret<0){errno=-ret;return -1;}return ret;
}
