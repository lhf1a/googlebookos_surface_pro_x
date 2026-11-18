/* SPDX-License-Identifier: MIT */
#include "qti-msm-vmbind-registry.h"
#include <errno.h>
#include <linux/dma-buf.h>
#include <linux/sync_file.h>
#include <stdlib.h>
#include <string.h>

struct qti_vm_range { uint64_t start, size; struct qti_vm_range *next; };
/* One owned sync-file per submission, shared by all private BO references. */
struct private_fence {
    int fd;
    size_t refs;
    bool seen;
    struct private_fence *next_seen;
};
struct qti_vm_private_access {
    uint32_t queue;
    struct private_fence *writer, *reader;
    struct qti_vm_private_access *next;
};
struct qti_vm_bo_record {
    struct qti_vm_mapping mapping;
    struct qti_vm_range *release_range; /* preallocated before MAP */
    int dma_buf_fd, fence;
    bool unmap_queued;
    bool prepare_seen;
    struct qti_vm_private_access *accesses;
    /* Retired-list link, or transient prepare chain while live and locked. */
    struct qti_vm_bo_record *next;
};
struct qti_vm_queue_fence {
    uint32_t queue;
    int fence;
    bool closed;
    struct qti_vm_queue_fence *next;
};

int qti_vm_real_fence_status(void *opaque, int fd)
{
    struct sync_file_info info = {0};
    int ret = qti_vm_real_ioctl(opaque, fd, SYNC_IOC_FILE_INFO, &info);
    return ret ? ret : info.status;
}
static int usable(struct qti_vm_registry *r)
{
    return r && r->ready && r->vm && r->vm->ready && !r->vm->lost ? 0 : -EPIPE;
}
static size_t index_for(struct qti_vm_registry *r, uint32_t handle)
{
    size_t lo=0,hi=r->live_count;
    while(lo<hi){size_t m=lo+(hi-lo)/2;if(r->live[m]->mapping.handle<handle)lo=m+1;else hi=m;}
    return lo;
}
static struct qti_vm_bo_record *find(struct qti_vm_registry *r, uint32_t handle)
{
    size_t i=r->lookup_hint;
    if(i>=r->live_count||r->live[i]->mapping.handle!=handle)i=index_for(r,handle);
    r->lookup_hint=i+1;
    return i<r->live_count && r->live[i]->mapping.handle==handle ? r->live[i] : NULL;
}
static void unref(struct qti_vm_registry *r,struct private_fence *f)
{
    if(f && !--f->refs){r->ops->close_fd(r->opaque,f->fd);free(f);}
}
static void clear_accesses(struct qti_vm_registry *r,struct qti_vm_bo_record *bo)
{
    r->template_valid=false;
    while(bo->accesses){struct qti_vm_private_access *p=bo->accesses;bo->accesses=p->next;
        unref(r,p->writer);unref(r,p->reader);free(p);}
}
static struct qti_vm_private_access *access_for(struct qti_vm_bo_record *bo,uint32_t queue)
{
    for(struct qti_vm_private_access *p=bo->accesses;p;p=p->next)if(p->queue==queue)return p;
    return NULL;
}
static void gather(struct private_fence **head,struct private_fence *f)
{
    if(f && !f->seen){f->seen=true;f->next_seen=*head;*head=f;}
}
int qti_vm_registry_prepare_sync_prepared(struct qti_vm_registry *r,
                                 const struct drm_msm_gem_submit *req,
                                 struct qti_vm_sync_batch *batch,
                                 struct qti_vm_prepared_bo *prepared_bo)
{
    if(usable(r))return -EPIPE;
    if(!req||!batch)return -EINVAL;
    if(req->nr_bos&&!req->bos)return -EFAULT;
    if(req->nr_bos && SIZE_MAX/req->nr_bos<sizeof(struct qti_vm_shared_access))return -EOVERFLOW;
    struct qti_vm_shared_access *access=req->nr_bos?calloc(req->nr_bos,sizeof(*access)):NULL;
    if(req->nr_bos&&!access)return -ENOMEM;
    const struct drm_msm_gem_submit_bo *bos=(void *)(uintptr_t)req->bos;
    struct private_fence *seen=NULL;
    struct qti_vm_bo_record *prepared=NULL;
    /* Entries point to individually allocated live records under the registry lock.
     * Empty submits do not replace the last useful table. Never cache a fence/FD. */
    size_t old_len=r->cache_len;
    bool same=req->nr_bos && old_len==req->nr_bos;
    for(size_t i=0;same&&i<old_len;i++)same=r->cache[i].handle==bos[i].handle;
    if(req->nr_bos>r->cache_cap && SIZE_MAX/req->nr_bos>=sizeof(*r->cache)){
        void *v=realloc(r->cache,req->nr_bos*sizeof(*r->cache));
        if(v){r->cache=v;r->cache_cap=req->nr_bos;}
    }
    bool template_hit=same && prepared_bo && r->template_valid &&
        r->template_queue==req->queueid && r->template_explicit==(req->flags&MSM_SUBMIT_NO_IMPLICIT);
    for(size_t i=0;template_hit&&i<old_len;i++)template_hit=r->cache[i].input_flags==bos[i].flags;
    if(req->nr_bos)r->template_valid=false;
    if(prepared_bo && req->nr_bos>r->template_cap && SIZE_MAX/req->nr_bos>=sizeof(*prepared_bo)){
        struct qti_vm_shared_access *a=malloc(req->nr_bos*sizeof(*a));
        struct qti_vm_prepared_bo *p=malloc(req->nr_bos*sizeof(*p));
        if(a&&p){free(r->template_access);free(r->template_prepared);r->template_access=a;r->template_prepared=p;r->template_cap=req->nr_bos;}
        else{free(a);free(p);}
    }
    bool store=req->nr_bos && req->nr_bos<=r->cache_cap;
    size_t reused=0;
    if(req->nr_bos)r->cache_len=0;
    bool ordered_unique=true;
    for(uint32_t i=1;!same && i<req->nr_bos;i++)
        if(bos[i-1].handle>=bos[i].handle){ordered_unique=false;break;}
    int ret=0,dependency=-1;
    if(template_hit){
        /* Bulk restore only static fields. Readers/writers and every shared
         * acquire fence are read again, including on other queues. */
        memcpy(access,r->template_access,old_len*sizeof(*access));
        memcpy(prepared_bo,r->template_prepared,old_len*sizeof(*prepared_bo));
        for(size_t i=0;i<old_len;i++){
            if(!prepared_bo[i].access || (access[i].access&QTI_VM_ACCESS_NO_IMPLICIT))continue;
            for(struct qti_vm_private_access *p=r->cache[i].record->accesses;p;p=p->next){
                gather(&seen,p->writer);
                if(access[i].access&MSM_SUBMIT_BO_WRITE)gather(&seen,p->reader);
            }
        }
        reused=old_len;
    } else for(uint32_t i=0;i<req->nr_bos;i++){
        uint32_t flags=bos[i].flags,rw=flags&(MSM_SUBMIT_BO_READ|MSM_SUBMIT_BO_WRITE);
        if(!rw||(flags&~MSM_SUBMIT_BO_FLAGS)){ret=-EINVAL;goto done;}
        uint32_t h=bos[i].handle;
        struct qti_vm_bo_record *bo;
        if(i<old_len && r->cache[i].handle==h){bo=r->cache[i].record;reused++;}
        else {if(same){ret=-EINVAL;goto done;}bo=find(r,h);}
        if(!bo){ret=-ENOENT;goto done;}
        if(store)r->cache[i]=(struct qti_cached_bo){.handle=h,.record=bo,.input_flags=flags};
        /* Reject even non-adjacent duplicates without sorting either table.
         * Registry lock owns these transient marks; every exit clears them. */
        if(!ordered_unique){
            if(bo->prepare_seen){ret=-EINVAL;goto done;}
            bo->prepare_seen=true;bo->next=prepared;prepared=bo;
        }
        bool explicit=!!(req->flags&MSM_SUBMIT_NO_IMPLICIT)||!!(flags&MSM_SUBMIT_BO_NO_IMPLICIT);
        access[i]=(struct qti_vm_shared_access){bos[i].handle,bo->dma_buf_fd,
            rw|(explicit?QTI_VM_ACCESS_NO_IMPLICIT:0)};
        if(prepared_bo)prepared_bo[i]=(struct qti_vm_prepared_bo){.mapping=&bo->mapping};
        if(bo->dma_buf_fd>=0)continue;
        /* Reserve publication metadata before the GPU submission. */
        struct qti_vm_private_access *slot=access_for(bo,req->queueid);
        if(!slot){
            struct qti_vm_private_access *p=calloc(1,sizeof(*p));if(!p){ret=-ENOMEM;goto done;}
            p->queue=req->queueid;p->next=bo->accesses;bo->accesses=p;slot=p;
        }
        if(prepared_bo)prepared_bo[i].access=slot;
        if(explicit)continue;
        for(struct qti_vm_private_access *p=bo->accesses;p;p=p->next){
            gather(&seen,p->writer);
            if(rw&MSM_SUBMIT_BO_WRITE)gather(&seen,p->reader);
        }
    }
    for(struct private_fence *f=seen;f;f=f->next_seen){
        if(dependency<0){dependency=r->ops->dup_fd(r->opaque,f->fd);
            if(dependency<0){ret=dependency;goto done;}}
        else{
            struct sync_merge_data m={.fd2=f->fd,.fence=-1};
            memcpy(m.name,"qti-private-deps",sizeof("qti-private-deps"));
            ret=r->ops->ioctl_fn(r->opaque,dependency,SYNC_IOC_MERGE,&m);
            if(ret)goto done;
            if(m.fence<0){ret=-EPROTO;goto done;}
            r->ops->close_fd(r->opaque,dependency);dependency=m.fence;
        }
    }
    bool save_template=store && prepared_bo && req->nr_bos<=r->template_cap;
    if(save_template && !template_hit){
        memcpy(r->template_access,access,req->nr_bos*sizeof(*access));
        memcpy(r->template_prepared,prepared_bo,req->nr_bos*sizeof(*prepared_bo));
    }
    ret=qti_vm_sync_begin_mixed_take(batch,r->ops,r->opaque,&access,req->nr_bos,
        req->flags&MSM_SUBMIT_FENCE_FD_IN?req->fence_fd:-1,r->mapping_fence,dependency);
    if(!ret && save_template){r->template_valid=true;r->template_queue=req->queueid;
        r->template_explicit=req->flags&MSM_SUBMIT_NO_IMPLICIT;r->template_hits+=template_hit;}
done:
    if(!ret && store){r->cache_len=req->nr_bos;r->cache_saved+=reused;r->cache_full+=same;}
    while(prepared){struct qti_vm_bo_record *bo=prepared;prepared=bo->next;
        bo->prepare_seen=false;bo->next=NULL;}
    while(seen){struct private_fence *f=seen;seen=f->next_seen;f->seen=false;f->next_seen=NULL;}
    if(dependency>=0)r->ops->close_fd(r->opaque,dependency);
    free(access);return ret;
}
int qti_vm_registry_prepare_sync(struct qti_vm_registry *r,
                                 const struct drm_msm_gem_submit *req,
                                 struct qti_vm_sync_batch *batch)
{
    return qti_vm_registry_prepare_sync_prepared(r,req,batch,NULL);
}

int qti_vm_registry_note_access_prepared(struct qti_vm_registry *r,
                                const struct drm_msm_gem_submit *req,int completion,
                                const struct qti_vm_prepared_bo *prepared_bo)
{
    if(usable(r))return -EPIPE;
    if(!req||completion<0){r->vm->lost=true;return -EINVAL;}
    const struct drm_msm_gem_submit_bo *bos=(void *)(uintptr_t)req->bos;
    struct private_fence *f=NULL;
    int ret=0;
    for(uint32_t i=0;i<req->nr_bos;i++){
        struct qti_vm_private_access *p;
        if(prepared_bo){
            if(!prepared_bo[i].mapping||prepared_bo[i].mapping->handle!=bos[i].handle){ret=-EINVAL;break;}
            p=prepared_bo[i].access;
            if(!p)continue; /* shared fence publication is handled by batch */
        }else{
            struct qti_vm_bo_record *bo=find(r,bos[i].handle);
            if(!bo){ret=-ENOENT;break;}
            if(bo->dma_buf_fd>=0)continue;
            p=access_for(bo,req->queueid);
        }
        if(!p||p->queue!=req->queueid){ret=-EINVAL;break;}
        if(!f){
            f=calloc(1,sizeof(*f));if(!f){ret=-ENOMEM;break;}
            f->fd=r->ops->dup_fd(r->opaque,completion);
            if(f->fd<0){ret=f->fd;free(f);f=NULL;break;}
            f->refs=1; /* local reference until all BOs have been updated */
        }
        if(bos[i].flags&MSM_SUBMIT_BO_WRITE){
            unref(r,p->writer);unref(r,p->reader);p->reader=NULL;p->writer=f;
        }else{unref(r,p->reader);p->reader=f;}
        f->refs++;
    }
    unref(r,f);
    if(ret)r->vm->lost=true;
    return ret;
}

int qti_vm_registry_note_access(struct qti_vm_registry *r,
                                const struct drm_msm_gem_submit *req,int completion)
{
    return qti_vm_registry_note_access_prepared(r,req,completion,NULL);
}

int qti_vm_registry_init(struct qti_vm_registry *r, struct qti_vm *vm,
                         const struct qti_vm_sync_ops *ops, void *opaque,
                         int (*status)(void *, int))
{
    if(!r||!vm||!vm->ready||vm->lost||!ops||!ops->dup_fd||!ops->close_fd||
       !ops->ioctl_fn||!status||!vm->page_size||
       (vm->page_size&(vm->page_size-1))) return -EINVAL;
    uint64_t page=vm->page_size,start=vm->va_start;
    if(vm->va_size>UINT64_MAX-start||start>UINT64_MAX-(page-1))return -ERANGE;
    uint64_t end=(start+vm->va_size)&~(page-1);
    start=(start+page-1)&~(page-1);if(!start)start=page;
    if(start>=end)return -ENOSPC;
    struct qti_vm_range *range=calloc(1,sizeof(*range));if(!range)return -ENOMEM;
    *range=(struct qti_vm_range){start,end-start,NULL};
    *r=(struct qti_vm_registry){.vm=vm,.ops=ops,.opaque=opaque,
        .fence_status=status,.free_ranges=range,.mapping_fence=-1,.ready=true};
    return 0;
}

static void release_range(struct qti_vm_registry *r,struct qti_vm_range *range)
{
    struct qti_vm_range **at=&r->free_ranges,*prev=NULL;
    while(*at && (*at)->start<range->start){prev=*at;at=&(*at)->next;}
    range->next=*at;*at=range;
    if(prev && prev->start+prev->size==range->start){
        prev->size+=range->size;prev->next=range->next;free(range);range=prev;
    }
    if(range->next && range->start+range->size==range->next->start){
        struct qti_vm_range *next=range->next;
        range->size+=next->size;range->next=next->next;free(next);
    }
}

int qti_vm_registry_attach_dma_buf(struct qti_vm_registry *r,uint32_t handle,int fd)
{
    if(usable(r))return -EPIPE;
    if(fd<0)return -EINVAL;
    struct qti_vm_bo_record *bo=find(r,handle);if(!bo)return -ENOENT;
    if(bo->dma_buf_fd>=0)return 0;
    int copy=r->ops->dup_fd(r->opaque,fd);if(copy<0)return copy;
    /* Publish before an export FD/name is returned or CPU_PREP is forwarded.
     * Preserve read vs write usage, including independent queues and explicit
     * submissions; no process-wide all-queue fence is substituted here. */
    for(struct qti_vm_private_access *p=bo->accesses;p;p=p->next){
        struct private_fence *fences[]={p->writer,p->reader};
        for(unsigned i=0;i<2;i++)if(fences[i]){
            struct dma_buf_import_sync_file in={.flags=i?DMA_BUF_SYNC_READ:DMA_BUF_SYNC_WRITE,
                .fd=fences[i]->fd};
            int ret=r->ops->ioctl_fn(r->opaque,copy,DMA_BUF_IOCTL_IMPORT_SYNC_FILE,&in);
            if(ret){r->ops->close_fd(r->opaque,copy);r->vm->lost=true;return ret;}
        }
    }
    clear_accesses(r,bo);
    bo->dma_buf_fd=copy;return 0;
}

int qti_vm_registry_add(struct qti_vm_registry *r,uint32_t handle,uint64_t size,
                        int dma_fd,enum qti_vm_handle_origin origin,
                        struct qti_vm_mapping *mapping)
{
    if(usable(r))return -EPIPE;
    if(!handle||!size||!mapping||dma_fd<-1||
       (origin!=QTI_VM_HANDLE_NEW&&origin!=QTI_VM_HANDLE_PRIME))return -EINVAL;
    uint64_t page=r->vm->page_size;
    if(size>UINT64_MAX-(page-1))return -EOVERFLOW;
    size=(size+page-1)&~(page-1);
    struct qti_vm_bo_record *existing=find(r,handle);
    if(existing){
        if(origin==QTI_VM_HANDLE_NEW)return -EEXIST;
        if(size!=existing->mapping.size)return -EINVAL;
        int ret=dma_fd>=0?qti_vm_registry_attach_dma_buf(r,handle,dma_fd):0;
        if(!ret)*mapping=existing->mapping;
        return ret;
    }
    int ret=qti_vm_registry_collect(r);if(ret)return ret;
    struct qti_vm_range **slot=&r->free_ranges;
    while(*slot && (*slot)->size<size)slot=&(*slot)->next;
    if(!*slot)return -ENOSPC;
    if(r->live_count==r->live_capacity){
        size_t capacity=r->live_capacity?r->live_capacity*2:16;
        if(capacity<r->live_capacity||capacity>SIZE_MAX/sizeof(*r->live))return -EOVERFLOW;
        void *p=realloc(r->live,capacity*sizeof(*r->live));if(!p)return -ENOMEM;
        r->live=p;r->live_capacity=capacity;
    }
    struct qti_vm_bo_record *bo=calloc(1,sizeof(*bo));if(!bo)return -ENOMEM;
    bo->dma_buf_fd=-1;bo->fence=-1;
    bo->release_range=calloc(1,sizeof(*bo->release_range));
    if(!bo->release_range){free(bo);return -ENOMEM;}
    if(dma_fd>=0){
        bo->dma_buf_fd=r->ops->dup_fd(r->opaque,dma_fd);
        if(bo->dma_buf_fd<0){ret=bo->dma_buf_fd;free(bo->release_range);free(bo);return ret;}
    }
    bo->mapping=(struct qti_vm_mapping){handle,(*slot)->start,size};
    *bo->release_range=(struct qti_vm_range){(*slot)->start,size,NULL};
    (*slot)->start+=size;(*slot)->size-=size;
    if(!(*slot)->size){struct qti_vm_range *used=*slot;*slot=used->next;free(used);}
    size_t i=index_for(r,handle);
    memmove(r->live+i+1,r->live+i,(r->live_count-i)*sizeof(*r->live));
    r->live[i]=bo;r->live_count++;
    int fence=-1;
    ret=qti_vm_map(r->vm,&bo->mapping,-1,&fence);
    /* On MAP failure keep the reservation/owned dma-buf until FD teardown:
     * an asynchronous mapping failure is not safely reversible in place.
     */
    if(ret){r->vm->lost=true;return ret;}
    if(r->mapping_fence>=0)r->ops->close_fd(r->opaque,r->mapping_fence);
    r->mapping_fence=fence;
    *mapping=bo->mapping;return 0;
}

int qti_vm_registry_resolve(void *registry,uint32_t handle,struct qti_vm_mapping *mapping)
{
    struct qti_vm_registry *r=registry;if(usable(r))return -EPIPE;
    if(!mapping)return -EINVAL;
    struct qti_vm_bo_record *bo=find(r,handle);if(!bo)return -ENOENT;
    *mapping=bo->mapping;return 0;
}
int qti_vm_registry_dma_buf(struct qti_vm_registry *r,uint32_t handle)
{
    if(usable(r))return -EPIPE;
    struct qti_vm_bo_record *bo=find(r,handle);return bo?bo->dma_buf_fd:-ENOENT;
}
int qti_vm_registry_mapping_fence(struct qti_vm_registry *r)
{
    return usable(r)?-EPIPE:r->mapping_fence;
}

int qti_vm_registry_note_submit(struct qti_vm_registry *r,uint32_t queue,int fence)
{
    if(usable(r))return -EPIPE;
    if(fence<0){r->vm->lost=true;return -EINVAL;}
    struct qti_vm_queue_fence *q=r->queues;
    while(q && q->queue!=queue)q=q->next;
    if(q && q->closed){r->vm->lost=true;return -EINVAL;}
    struct qti_vm_queue_fence *created=NULL;
    if(!q){
        created=calloc(1,sizeof(*created));
        if(!created){r->vm->lost=true;return -ENOMEM;}
        created->queue=queue;created->fence=-1;q=created;
    }
    int copy=r->ops->dup_fd(r->opaque,fence);
    if(copy<0){free(created);r->vm->lost=true;return copy;}
    if(q->fence>=0)r->ops->close_fd(r->opaque,q->fence);
    q->fence=copy;
    if(created){q->next=r->queues;r->queues=q;}
    return 0;
}
void qti_vm_registry_close_queue(struct qti_vm_registry *r,uint32_t queue)
{
    if(!r||!r->ready)return;
    for(struct qti_vm_queue_fence *q=r->queues;q;q=q->next)
        if(q->queue==queue){q->closed=true;return;}
}

static int retirement_fence(struct qti_vm_registry *r)
{
    if(r->mapping_fence<0)return -EINVAL;
    int aggregate=r->ops->dup_fd(r->opaque,r->mapping_fence);
    if(aggregate<0)return aggregate;
    for(struct qti_vm_queue_fence *q=r->queues;q;q=q->next){
        struct sync_merge_data m={.fd2=q->fence,.fence=-1};
        memcpy(m.name,"qti-vm-retire",sizeof("qti-vm-retire"));
        int ret=r->ops->ioctl_fn(r->opaque,aggregate,SYNC_IOC_MERGE,&m);
        r->ops->close_fd(r->opaque,aggregate);
        if(ret)return ret;
        if(m.fence<0)return -EPROTO;
        aggregate=m.fence;
    }
    return aggregate;
}
int qti_vm_registry_retire(struct qti_vm_registry *r,uint32_t handle)
{
    if(usable(r))return -EPIPE;
    size_t i=index_for(r,handle);
    if(i==r->live_count||r->live[i]->mapping.handle!=handle)return -ENOENT;
    int fence=retirement_fence(r);if(fence<0)return fence;
    struct qti_vm_bo_record *bo=r->live[i];
    clear_accesses(r,bo); /* retirement fence above retains all GPU work */
    memmove(r->live+i,r->live+i+1,(r->live_count-i-1)*sizeof(*r->live));
    r->live_count--; r->cache_len=0; /* invalidate before handle or record reuse */
    bo->fence=fence;bo->next=r->retired;r->retired=bo;r->retired_count++;
    return 0;
}

int qti_vm_registry_collect(struct qti_vm_registry *r)
{
    if(usable(r))return -EPIPE;
    struct qti_vm_bo_record **at=&r->retired;
    while(*at){
        struct qti_vm_bo_record *bo=*at;
        int status=r->fence_status(r->opaque,bo->fence);
        if(status<0){r->vm->lost=true;return status;}
        if(!status){at=&bo->next;continue;}
        if(!bo->unmap_queued){
            int fence=-1,ret=qti_vm_unmap(r->vm,&bo->mapping,bo->fence,&fence);
            if(ret){r->vm->lost=true;return ret;}
            r->ops->close_fd(r->opaque,bo->fence);bo->fence=fence;bo->unmap_queued=true;
            /* Do not assume immediate completion even if UNMAP ioctl returned. */
            at=&bo->next;continue;
        }
        r->ops->close_fd(r->opaque,bo->fence);
        if(bo->dma_buf_fd>=0)r->ops->close_fd(r->opaque,bo->dma_buf_fd);
        release_range(r,bo->release_range);
        *at=bo->next;free(bo);r->retired_count--;
    }
    struct qti_vm_queue_fence **q=&r->queues;
    while(*q){
        struct qti_vm_queue_fence *item=*q;
        if(!item->closed){q=&item->next;continue;}
        int status=r->fence_status(r->opaque,item->fence);
        if(status<0){r->vm->lost=true;return status;}
        if(!status){q=&item->next;continue;}
        r->ops->close_fd(r->opaque,item->fence);*q=item->next;free(item);
    }
    return 0;
}

int qti_vm_registry_finish(struct qti_vm_registry *r,bool fd_closed)
{
    if(!r||!r->ready)return 0;
    if(!fd_closed && (r->live_count||r->retired_count))return -EBUSY;
    for(size_t i=0;i<r->live_count;i++){
        struct qti_vm_bo_record *bo=r->live[i];
        if(bo->dma_buf_fd>=0)r->ops->close_fd(r->opaque,bo->dma_buf_fd);
        clear_accesses(r,bo);
        free(bo->release_range);free(bo);
    }
    while(r->retired){struct qti_vm_bo_record *bo=r->retired;r->retired=bo->next;
        if(bo->fence>=0)r->ops->close_fd(r->opaque,bo->fence);
        if(bo->dma_buf_fd>=0)r->ops->close_fd(r->opaque,bo->dma_buf_fd);
        clear_accesses(r,bo);
        free(bo->release_range);free(bo);
    }
    while(r->free_ranges){struct qti_vm_range *n=r->free_ranges;r->free_ranges=n->next;free(n);}
    while(r->queues){struct qti_vm_queue_fence *q=r->queues;r->queues=q->next;r->ops->close_fd(r->opaque,q->fence);free(q);}
    if(r->mapping_fence>=0)r->ops->close_fd(r->opaque,r->mapping_fence);
    free(r->template_access);free(r->template_prepared);free(r->cache);free(r->live);*r=(struct qti_vm_registry){.mapping_fence=-1};return 0;
}
