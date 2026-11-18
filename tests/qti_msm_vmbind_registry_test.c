/* SPDX-License-Identifier: MIT */
#include "qti-msm-vmbind-registry.h"
#include <assert.h>
#include <errno.h>
#include <linux/sync_file.h>
#include <stdio.h>

struct slot { bool alive; unsigned deps; };
struct fake {
    struct slot slots[4096];
    int next, maps, unmaps;
    unsigned done;
    bool fail_map, fail_dup, fail_merge, status_error;
};
static void setup(struct fake *f)
{
    *f=(struct fake){.next=100};
    f->slots[10]=(struct slot){true,1};f->slots[11]=(struct slot){true,2};
    f->slots[12]=(struct slot){true,9};f->slots[30]=(struct slot){true,0};
}
static int newfd(struct fake *f,unsigned deps)
{
    assert(f->next<4096);f->slots[f->next]=(struct slot){true,deps};return f->next++;
}
static int dupfd(void *opaque,int fd)
{
    struct fake *f=opaque;assert(f->slots[fd].alive);
    if(f->fail_dup){f->fail_dup=false;return -EMFILE;}
    return newfd(f,f->slots[fd].deps);
}
static void closefd(void *opaque,int fd)
{
    struct fake *f=opaque;assert(fd>=100&&f->slots[fd].alive);f->slots[fd].alive=false;
}
static int status(void *opaque,int fd)
{
    struct fake *f=opaque;assert(f->slots[fd].alive);
    return f->status_error?-EIO:!(f->slots[fd].deps&~f->done);
}
static int io(void *opaque,int fd,unsigned long request,void *arg)
{
    struct fake *f=opaque;
    if(request==DRM_IOCTL_MSM_VM_BIND){
        assert(fd==7);struct drm_msm_vm_bind *b=arg;
        if(f->fail_map)return -ENOMEM;
        if(b->op.op==MSM_VM_BIND_OP_MAP){f->maps++;b->fence_fd=newfd(f,0);}
        else{assert(b->op.op==MSM_VM_BIND_OP_UNMAP);assert(status(f,b->fence_fd)==1);f->unmaps++;b->fence_fd=newfd(f,4);}
    }else{
        assert(request==SYNC_IOC_MERGE&&f->slots[fd].alive);
        struct sync_merge_data *m=arg;assert(f->slots[m->fd2].alive);
        if(f->fail_merge){f->fail_merge=false;return -ENOMEM;}
        m->fence=newfd(f,f->slots[fd].deps|f->slots[m->fd2].deps);
    }
    return 0;
}
static void no_leaks(struct fake *f)
{
    for(int i=100;i<f->next;i++)assert(!f->slots[i].alive);
    assert(f->slots[10].alive&&f->slots[11].alive&&f->slots[12].alive&&f->slots[30].alive);
}
static struct qti_vm vm_for(struct fake *f,uint64_t pages)
{
    return (struct qti_vm){.fd=7,.ioctl_fn=io,.opaque=f,.va_start=0x100000000ULL,
        .va_size=4096*pages,.page_size=4096,.bind_queue=9,.enabled=true,.ready=true};
}
int main(void)
{
    const struct qti_vm_sync_ops ops={io,dupfd,closefd};
    struct fake f;setup(&f);struct qti_vm vm=vm_for(&f,3);struct qti_vm_registry reg;
    assert(!qti_vm_registry_init(&reg,&vm,&ops,&f,status));
    struct qti_vm_mapping a,b,c,again;
    assert(!qti_vm_registry_add(&reg,10,4095,30,QTI_VM_HANDLE_NEW,&a));
    assert(a.size==4096&&a.iova==vm.va_start);
    int owned=qti_vm_registry_dma_buf(&reg,10);assert(owned>=100);
    int calls=f.maps;
    assert(!qti_vm_registry_add(&reg,10,4096,30,QTI_VM_HANDLE_PRIME,&again));
    assert(again.iova==a.iova&&f.maps==calls&&reg.live_count==1&&qti_vm_registry_dma_buf(&reg,10)==owned);
    assert(qti_vm_registry_add(&reg,10,4096,-1,QTI_VM_HANDLE_NEW,&again)==-EEXIST);
    assert(qti_vm_registry_add(&reg,10,8192,30,QTI_VM_HANDLE_PRIME,&again)==-EINVAL);
    assert(!qti_vm_registry_add(&reg,20,4096,-1,QTI_VM_HANDLE_NEW,&b));
    assert(b.iova==a.iova+4096);
    assert(!qti_vm_registry_note_submit(&reg,7,10));
    assert(!qti_vm_registry_note_submit(&reg,8,11));
    assert(!qti_vm_registry_note_submit(&reg,7,12));
    f.fail_merge=true;assert(qti_vm_registry_retire(&reg,10)==-ENOMEM&&reg.live_count==2);
    assert(!qti_vm_registry_retire(&reg,10)&&reg.retired_count==1);
    assert(qti_vm_registry_resolve(&reg,10,&again)==-ENOENT);
    assert(!qti_vm_registry_add(&reg,10,4096,-1,QTI_VM_HANDLE_NEW,&again));
    assert(again.iova==a.iova+8192); /* same integer GEM handle, different generation */
    assert(qti_vm_registry_add(&reg,30,4096,-1,QTI_VM_HANDLE_NEW,&c)==-ENOSPC);
    assert(!qti_vm_registry_collect(&reg)&&f.unmaps==0);
    f.done=1;assert(!qti_vm_registry_collect(&reg)&&f.unmaps==0);
    f.done=9;assert(!qti_vm_registry_collect(&reg)&&f.unmaps==0); /* queue 8 still pending */
    f.done=11;assert(!qti_vm_registry_collect(&reg)&&f.unmaps==1);
    assert(qti_vm_registry_add(&reg,30,4096,-1,QTI_VM_HANDLE_NEW,&c)==-ENOSPC); /* UNMAP still pending */
    f.done=15;assert(!qti_vm_registry_collect(&reg)&&reg.retired_count==0);
    assert(!qti_vm_registry_add(&reg,30,4096,-1,QTI_VM_HANDLE_NEW,&c)&&c.iova==a.iova);
    assert(qti_vm_registry_finish(&reg,false)==-EBUSY);
    for(unsigned h=10;h<=30;h+=10)assert(!qti_vm_registry_retire(&reg,h));
    qti_vm_registry_close_queue(&reg,7);
    assert(!qti_vm_registry_collect(&reg));assert(!qti_vm_registry_collect(&reg));
    assert(!reg.live_count&&!reg.retired_count);
    assert(!qti_vm_registry_finish(&reg,false));no_leaks(&f);
    setup(&f);vm=vm_for(&f,2);assert(!qti_vm_registry_init(&reg,&vm,&ops,&f,status));
    f.fail_dup=true;assert(qti_vm_registry_add(&reg,1,4096,30,QTI_VM_HANDLE_NEW,&a)==-EMFILE&&!reg.live_count);
    assert(!qti_vm_registry_add(&reg,1,4096,30,QTI_VM_HANDLE_NEW,&a)&&a.iova==vm.va_start);
    f.fail_dup=true;assert(qti_vm_registry_note_submit(&reg,1,10)==-EMFILE&&vm.lost);
    assert(qti_vm_registry_retire(&reg,1)==-EPIPE);
    assert(!qti_vm_registry_finish(&reg,true));no_leaks(&f);
    setup(&f);vm=vm_for(&f,2);assert(!qti_vm_registry_init(&reg,&vm,&ops,&f,status));
    f.fail_map=true;assert(qti_vm_registry_add(&reg,1,4096,30,QTI_VM_HANDLE_NEW,&a)==-ENOMEM&&vm.lost&&reg.live_count==1);
    assert(!qti_vm_registry_finish(&reg,true));no_leaks(&f);
    setup(&f);vm=vm_for(&f,2);assert(!qti_vm_registry_init(&reg,&vm,&ops,&f,status));
    assert(!qti_vm_registry_add(&reg,1,4096,-1,QTI_VM_HANDLE_NEW,&a));
    assert(!qti_vm_registry_retire(&reg,1));f.status_error=true;
    assert(qti_vm_registry_collect(&reg)==-EIO&&vm.lost);
    assert(!qti_vm_registry_finish(&reg,true));no_leaks(&f);
    setup(&f);vm=vm_for(&f,128);f.done=4;
    assert(!qti_vm_registry_init(&reg,&vm,&ops,&f,status));
    for(unsigned i=0;i<128;i++)assert(!qti_vm_registry_add(&reg,128-i,4096,-1,QTI_VM_HANDLE_NEW,&a));
    for(unsigned i=0;i<128;i++)assert(!qti_vm_registry_retire(&reg,1+(i*37)%128));
    assert(!qti_vm_registry_collect(&reg));assert(!qti_vm_registry_collect(&reg));
    assert(!qti_vm_registry_add(&reg,1,128*4096,-1,QTI_VM_HANDLE_NEW,&a)&&a.iova==vm.va_start);
    assert(!qti_vm_registry_finish(&reg,true));no_leaks(&f);
    puts("PASS PRIME alias maps once; reused GEM handle has new VA; all-queue retirement; no reuse before UNMAP signal; coalescing; failure poison; FD ownership");
    return 0;
}
