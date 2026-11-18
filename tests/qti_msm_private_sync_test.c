/* SPDX-License-Identifier: MIT */
#include "qti-msm-vmbind-registry.h"
#include <assert.h>
#include <errno.h>
#include <linux/dma-buf.h>
#include <linux/sync_file.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct slot { bool alive, dma; unsigned deps; };
struct fake {
    struct slot fd[65536];
    int next, exports, imports, merges, dups, fail_call, calls;
    unsigned shared_write, shared_read;
};
static struct fake f;
static int alloc(struct fake *f,struct slot value)
{
    assert(f->next<65536);f->fd[f->next]=value;return f->next++;
}
static int dupfd(void *p,int fd)
{
    struct fake *f=p;assert(fd>=0&&f->fd[fd].alive);
    if(++f->calls==f->fail_call)return -EMFILE;
    f->dups++;return alloc(f,f->fd[fd]);
}
static void closefd(void *p,int fd)
{
    struct fake *f=p;assert(fd>=100&&f->fd[fd].alive);f->fd[fd].alive=false;
}
static int status(void *p,int fd){struct fake *f=p;assert(f->fd[fd].alive);return 0;}
static int io(void *p,int fd,unsigned long req,void *arg)
{
    struct fake *f=p;
    if(++f->calls==f->fail_call)return -EIO;
    if(req==DRM_IOCTL_MSM_VM_BIND){
        assert(fd==7);struct drm_msm_vm_bind *b=arg;assert(b->op.op==MSM_VM_BIND_OP_MAP);
        b->fence_fd=alloc(f,(struct slot){true,false,0});return 0;
    }
    assert(f->fd[fd].alive);
    if(req==SYNC_IOC_MERGE){
        struct sync_merge_data *m=arg;assert(f->fd[m->fd2].alive);
        f->merges++;m->fence=alloc(f,(struct slot){true,false,f->fd[fd].deps|f->fd[m->fd2].deps});
    }else if(req==DMA_BUF_IOCTL_EXPORT_SYNC_FILE){
        struct dma_buf_export_sync_file *e=arg;assert(f->fd[fd].dma);f->exports++;
        e->fd=alloc(f,(struct slot){true,false,f->shared_write|((e->flags&DMA_BUF_SYNC_WRITE)?f->shared_read:0)});
    }else{
        assert(req==DMA_BUF_IOCTL_IMPORT_SYNC_FILE&&f->fd[fd].dma);
        struct dma_buf_import_sync_file *in=arg;assert(f->fd[in->fd].alive);f->imports++;
        if(in->flags&DMA_BUF_SYNC_WRITE)f->shared_write|=f->fd[in->fd].deps;
        else f->shared_read|=f->fd[in->fd].deps;
    }
    return 0;
}
static const struct qti_vm_sync_ops ops={io,dupfd,closefd};
static void init(struct qti_vm *vm,struct qti_vm_registry *r)
{
    memset(&f,0,sizeof(f));f.next=100;f.shared_write=128;
    unsigned deps[]={1,2,5,9,16,32};
    for(unsigned i=0;i<6;i++)f.fd[10+i]=(struct slot){true,false,deps[i]};
    f.fd[30]=(struct slot){true,true,0};
    *vm=(struct qti_vm){.fd=7,.ioctl_fn=io,.opaque=&f,.va_start=0x100000000ULL,
        .va_size=4096ULL*16384,.page_size=4096,.bind_queue=9,.enabled=true,.ready=true};
    assert(!qti_vm_registry_init(r,vm,&ops,&f,status));
}
static void finish(struct qti_vm_registry *r)
{
    assert(!qti_vm_registry_finish(r,true));
    for(int i=100;i<f.next;i++)assert(!f.fd[i].alive);
    for(int i=10;i<16;i++)assert(f.fd[i].alive);
    assert(f.fd[30].alive);
}
static void add(struct qti_vm_registry *r,uint32_t handle,int fd)
{
    struct qti_vm_mapping map;
    assert(!qti_vm_registry_add(r,handle,4096,fd,QTI_VM_HANDLE_NEW,&map));
}
static unsigned prepare(struct qti_vm_registry *r,struct drm_msm_gem_submit *req)
{
    struct qti_vm_sync_batch batch={0};struct drm_msm_gem_submit out;
    assert(!qti_vm_registry_prepare_sync(r,req,&batch));
    assert(!qti_vm_sync_prepare_request(&batch,req,&out));
    assert(out.flags&MSM_SUBMIT_NO_IMPLICIT);
    unsigned deps=f.fd[batch.wait_fd].deps;
    qti_vm_sync_finish(&batch);return deps;
}
int main(void)
{
    struct qti_vm vm;struct qti_vm_registry r;
    init(&vm,&r);add(&r,1,-1);add(&r,2,-1);
    struct drm_msm_gem_submit_bo bo={.handle=1,.flags=MSM_SUBMIT_BO_WRITE};
    struct drm_msm_gem_submit req={.flags=MSM_PIPE_3D0,.bos=(uintptr_t)&bo,.nr_bos=1,.queueid=1};
    assert(prepare(&r,&req)==0);assert(!qti_vm_registry_note_access(&r,&req,10));
    bo.handle=2;req.queueid=2;assert(prepare(&r,&req)==0); /* unrelated resource must not wait */
    assert(!qti_vm_registry_note_access(&r,&req,11));
    bo.handle=1;bo.flags=MSM_SUBMIT_BO_READ;
    assert(prepare(&r,&req)==1);assert(!qti_vm_registry_note_access(&r,&req,12));
    req.queueid=3;assert(prepare(&r,&req)==1); /* no read->read dependency */
    assert(!qti_vm_registry_note_access(&r,&req,13));
    req.queueid=4;bo.flags=MSM_SUBMIT_BO_WRITE;assert(prepare(&r,&req)==13);
    bo.flags|=MSM_SUBMIT_BO_NO_IMPLICIT;assert(prepare(&r,&req)==0);
    assert(!qti_vm_registry_note_access(&r,&req,14)); /* explicit work still publishes */
    bo.flags=MSM_SUBMIT_BO_READ;assert(prepare(&r,&req)==17);
    req.flags|=MSM_SUBMIT_NO_IMPLICIT;assert(prepare(&r,&req)==0);req.flags&=~MSM_SUBMIT_NO_IMPLICIT;
    assert(f.exports==0&&f.imports==0);
    assert(!qti_vm_registry_attach_dma_buf(&r,1,30));
    assert(f.shared_write==(128|1|16)&&f.shared_read==(5|9));
    int published=f.imports;assert(!qti_vm_registry_attach_dma_buf(&r,1,30)&&f.imports==published);
    assert(prepare(&r,&req)==145);bo.flags=MSM_SUBMIT_BO_WRITE;assert(prepare(&r,&req)==157);
    struct drm_msm_gem_submit_bo both[]={{MSM_SUBMIT_BO_WRITE,1,0},{MSM_SUBMIT_BO_WRITE,2,0}};
    req.bos=(uintptr_t)both;req.nr_bos=2;assert(prepare(&r,&req)==159);
    req.flags|=MSM_SUBMIT_NO_IMPLICIT;
    int exports=f.exports;struct qti_vm_sync_batch batch={0};struct drm_msm_gem_submit out;
    assert(!qti_vm_registry_prepare_sync(&r,&req,&batch)&&f.exports==exports);
    req.flags&=~MSM_SUBMIT_NO_IMPLICIT;assert(qti_vm_sync_prepare_request(&batch,&req,&out)==-EINVAL);
    req.flags|=MSM_SUBMIT_NO_IMPLICIT;assert(!qti_vm_sync_prepare_request(&batch,&req,&out));
    assert(!qti_vm_registry_note_access(&r,&req,15));
    assert(!qti_vm_sync_publish(&batch,15)&&f.imports==published+1);qti_vm_sync_finish(&batch);
    finish(&r);

    /* Mixed unsorted table keeps handle/access alignment and all validation. */
    init(&vm,&r);add(&r,1,-1);add(&r,2,-1);add(&r,3,30);
    struct drm_msm_gem_submit_bo shuffled[]={{MSM_SUBMIT_BO_WRITE,2,0},
        {MSM_SUBMIT_BO_READ,1,0},{MSM_SUBMIT_BO_READ,3,0}};
    req=(struct drm_msm_gem_submit){.flags=MSM_PIPE_3D0,.bos=(uintptr_t)shuffled,.nr_bos=3};
    assert(prepare(&r,&req)==128);assert(!qti_vm_registry_note_access(&r,&req,10));
    struct drm_msm_gem_submit_bo saved=shuffled[2];shuffled[2]=shuffled[0];
    assert(qti_vm_registry_prepare_sync(&r,&req,&batch)==-EINVAL&&!vm.lost);
    shuffled[2]=saved;assert(prepare(&r,&req)==129); /* duplicate failure cleared marks */
    assert(!qti_vm_registry_prepare_sync(&r,&req,&batch));
    shuffled[0].handle=1;assert(qti_vm_sync_prepare_request(&batch,&req,&out)==-ENODATA);
    shuffled[0].handle=2;assert(!qti_vm_sync_prepare_request(&batch,&req,&out));
    qti_vm_sync_finish(&batch);
    f.fail_call=f.calls+1;
    assert(qti_vm_registry_prepare_sync(&r,&req,&batch)<0&&!vm.lost);
    f.fail_call=0;assert(prepare(&r,&req)==129); /* sync failure cleared marks */
    finish(&r);
    puts("PASS unsorted private/shared BO alignment; duplicate/stale-order rejection; prepare marks cleared after duplicate and sync failure");

    /* Promotion failure retains dependencies until teardown and poisons VM. */
    init(&vm,&r);add(&r,1,-1);req=(struct drm_msm_gem_submit){.flags=MSM_PIPE_3D0,.bos=(uintptr_t)&bo,.nr_bos=1};
    assert(prepare(&r,&req)==0);assert(!qti_vm_registry_note_access(&r,&req,10));
    f.fail_call=f.calls+2;assert(qti_vm_registry_attach_dma_buf(&r,1,30)==-EIO&&vm.lost);finish(&r);

    /* 9000 objects sharing one completion require one dependency merge. */
    init(&vm,&r);const unsigned n=9000;
    struct drm_msm_gem_submit_bo *many=calloc(n,sizeof(*many));assert(many);
    for(unsigned i=0;i<n;i++){add(&r,i+1,-1);many[i]=(struct drm_msm_gem_submit_bo){MSM_SUBMIT_BO_WRITE,i+1,0};}
    req=(struct drm_msm_gem_submit){.flags=MSM_PIPE_3D0,.bos=(uintptr_t)many,.nr_bos=n};
    assert(prepare(&r,&req)==0);assert(!qti_vm_registry_note_access(&r,&req,10));
    int merges=f.merges,dups=f.dups;
    assert(prepare(&r,&req)==1&&f.merges-merges==1&&f.dups-dups==3);
    assert(!qti_vm_registry_note_access(&r,&req,11));assert(f.exports==0&&f.imports==0);
    assert(prepare(&r,&req)==2);finish(&r);free(many);
    puts("PASS per-resource multi-queue R/W dependencies; no unrelated/read-read waits; NO_IMPLICIT still publishes; export promotion; partial failure teardown; 9000 BOs share one dependency merge; no FD leaks");
    return 0;
}
