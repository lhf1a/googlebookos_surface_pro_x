/* SPDX-License-Identifier: MIT */
#include "qti-msm-vmbind-sync.h"
#include <assert.h>
#include <errno.h>
#include <linux/dma-buf.h>
#include <linux/sync_file.h>
#include <stdio.h>
#include <string.h>

struct slot { bool alive; int kind; unsigned deps; };
struct fake {
    struct slot fd[512];
    int next, calls, fail_at, imports, dma_dups, dma_closes;
    unsigned import_access[2];
};
static void setup(struct fake *f)
{
    *f=(struct fake){.next=100};
    f->fd[10]=(struct slot){true,1,4}; f->fd[11]=(struct slot){true,1,8};
    f->fd[20]=(struct slot){true,2,1}; f->fd[21]=(struct slot){true,2,2};
    f->fd[22]=(struct slot){true,2,16};
}
static bool fail(struct fake *f) { return ++f->calls==f->fail_at; }
static int allocate(struct fake *f,struct slot slot)
{
    assert(f->next<512); f->fd[f->next]=slot;return f->next++;
}
static int dup_fd(void *opaque,int fd)
{
    struct fake *f=opaque;assert(f->fd[fd].alive);
    if(fail(f))return -EIO;
    if(f->fd[fd].kind==1)f->dma_dups++;
    return allocate(f,f->fd[fd]);
}
static void close_fd(void *opaque,int fd)
{
    struct fake *f=opaque;assert(fd>=100&&fd<512&&f->fd[fd].alive);
    if(f->fd[fd].kind==1)f->dma_closes++;
    f->fd[fd].alive=false;
}
static int io(void *opaque,int fd,unsigned long request,void *arg)
{
    struct fake *f=opaque;assert(f->fd[fd].alive);
    if(fail(f))return -EIO;
    if(request==DMA_BUF_IOCTL_EXPORT_SYNC_FILE){
        struct dma_buf_export_sync_file *e=arg;assert(f->fd[fd].kind==1);
        assert(e->flags==DMA_BUF_SYNC_READ || e->flags==DMA_BUF_SYNC_RW);
        e->fd=allocate(f,(struct slot){true,2,f->fd[fd].deps});
    }else if(request==SYNC_IOC_MERGE){
        struct sync_merge_data *m=arg;assert(f->fd[fd].kind==2);
        assert(f->fd[m->fd2].alive&&f->fd[m->fd2].kind==2);
        m->fence=allocate(f,(struct slot){true,2,f->fd[fd].deps|f->fd[m->fd2].deps});
    }else{
        assert(request==DMA_BUF_IOCTL_IMPORT_SYNC_FILE);
        struct dma_buf_import_sync_file *i=arg;
        assert(f->fd[fd].kind==1&&i->fd==22&&f->fd[22].alive);
        assert(f->imports<2);f->import_access[f->imports++]=i->flags;
    }
    return 0;
}
static void no_leaks(struct fake *f)
{
    for(int i=100;i<f->next;i++)assert(!f->fd[i].alive);
    assert(f->fd[10].alive&&f->fd[11].alive&&f->fd[20].alive&&f->fd[21].alive&&f->fd[22].alive);
}
int main(void)
{
    const struct qti_vm_sync_ops ops={io,dup_fd,close_fd};
    struct qti_vm_shared_access accesses[]={{2,11,DMA_BUF_SYNC_RW},{1,10,DMA_BUF_SYNC_READ}};
    struct fake f;setup(&f);struct qti_vm_sync_batch batch={0};
    assert(!qti_vm_sync_begin(&batch,&ops,&f,accesses,2,20,21));
    int calls=f.calls;
    assert(batch.state==QTI_VM_SYNC_READY&&batch.count==2&&f.fd[batch.wait_fd].deps==15);
    struct drm_msm_gem_submit_bo bos[]={{3,2,0},{1,1,0}};
    struct drm_msm_gem_submit legacy={.flags=MSM_PIPE_3D0|MSM_SUBMIT_FENCE_FD_IN,
        .fence_fd=20,.queueid=7,.nr_bos=2,.bos=(uintptr_t)bos,.fence=53};
    struct drm_msm_gem_submit out={0};
    assert(!qti_vm_sync_prepare_request(&batch,&legacy,&out));
    assert((out.flags&MSM_SUBMIT_NO_IMPLICIT)&&(out.flags&MSM_SUBMIT_FENCE_FD_OUT));
    assert(out.fence_fd==batch.wait_fd&&out.queueid==7&&out.fence==53&&out.bos==legacy.bos);
    assert(legacy.fence_fd==20 && !(legacy.flags&MSM_SUBMIT_NO_IMPLICIT));
    legacy.nr_bos=1;assert(qti_vm_sync_prepare_request(&batch,&legacy,&out)==-ENODATA);legacy.nr_bos=2;
    legacy.fence_fd=21;assert(qti_vm_sync_prepare_request(&batch,&legacy,&out)==-EINVAL);legacy.fence_fd=20;
    bos[1].flags=3;assert(qti_vm_sync_prepare_request(&batch,&legacy,&out)==-ENODATA);bos[1].flags=1;
    bos[1].handle=2;assert(qti_vm_sync_prepare_request(&batch,&legacy,&out)<0);bos[1].handle=1;
    assert(!qti_vm_sync_publish(&batch,22)&&f.imports==2);
    assert(f.import_access[0]==DMA_BUF_SYNC_READ&&f.import_access[1]==DMA_BUF_SYNC_RW);
    assert(qti_vm_sync_publish(&batch,22)==-EINVAL);
    qti_vm_sync_finish(&batch);qti_vm_sync_finish(&batch);no_leaks(&f);
    for(int i=1;i<=calls;i++) {
        setup(&f);f.fail_at=i;
        assert(qti_vm_sync_begin(&batch,&ops,&f,accesses,2,20,21)==-EIO);
        assert(batch.state==QTI_VM_SYNC_EMPTY);no_leaks(&f);
    }
    setup(&f);assert(!qti_vm_sync_begin(&batch,&ops,&f,accesses,2,20,21));
    f.fail_at=f.calls+2;
    assert(qti_vm_sync_publish(&batch,22)==-EIO&&f.imports==1&&batch.state==QTI_VM_SYNC_FAILED);
    int old_calls=f.calls;assert(qti_vm_sync_publish(&batch,22)==-EINVAL&&f.calls==old_calls);
    qti_vm_sync_finish(&batch);no_leaks(&f);
    setup(&f);accesses[1].handle=2;
    assert(qti_vm_sync_begin(&batch,&ops,&f,accesses,2,-1,-1)==-EINVAL&&f.calls==0);no_leaks(&f);
    accesses[1].handle=1;
    assert(!qti_vm_sync_begin(&batch,&ops,&f,NULL,0,-1,-1));
    legacy=(struct drm_msm_gem_submit){.flags=MSM_PIPE_3D0,.fence_fd=-1};
    assert(!qti_vm_sync_prepare_request(&batch,&legacy,&out)&&!(out.flags&MSM_SUBMIT_FENCE_FD_IN));
    assert(!qti_vm_sync_publish(&batch,22));qti_vm_sync_finish(&batch);no_leaks(&f);
    setup(&f);assert(!qti_vm_sync_begin_borrowed(&batch,&ops,&f,accesses,2,20,21));
    int borrowed_calls=f.calls;
    assert(borrowed_calls==calls-2&&f.dma_dups==0&&f.fd[batch.wait_fd].deps==15);
    assert(!qti_vm_sync_publish(&batch,22)&&f.imports==2);
    qti_vm_sync_finish(&batch);no_leaks(&f);assert(f.dma_closes==0);
    for(int i=1;i<=borrowed_calls;i++){
        setup(&f);f.fail_at=i;
        assert(qti_vm_sync_begin_borrowed(&batch,&ops,&f,accesses,2,20,21)==-EIO);
        no_leaks(&f);assert(f.dma_dups==0&&f.dma_closes==0);
    }
    setup(&f);assert(!qti_vm_sync_begin_borrowed(&batch,&ops,&f,accesses,2,20,21));
    f.fail_at=f.calls+2;
    assert(qti_vm_sync_publish(&batch,22)==-EIO&&f.imports==1&&batch.state==QTI_VM_SYNC_FAILED);
    qti_vm_sync_finish(&batch);no_leaks(&f);assert(f.dma_dups==0&&f.dma_closes==0);
    puts("PASS borrowed registry FDs: no per-BO dup/close; same dependency union and release publications; all begin failures and partial publish preserve borrowed inputs");
    puts("PASS original+map+dma-buf fence merge; read/write release; complete BO coverage; borrowed FDs retained; all prepare failures leak-free; partial publish failure not retried");
    return 0;
}
