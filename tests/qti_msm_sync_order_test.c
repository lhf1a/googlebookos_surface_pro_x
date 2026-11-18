/* SPDX-License-Identifier: MIT */
/* Same real registry/sync code, fake MAPs, no GPU calls. Counts actual qsorts. */
#include "qti-msm-vmbind-registry.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
static unsigned sorts;
static int owned;
void __real_qsort(void *,size_t,size_t,int (*)(const void *,const void *));
void __wrap_qsort(void *p,size_t n,size_t size,int (*cmp)(const void *,const void *))
{ sorts++;__real_qsort(p,n,size,cmp); }
static int copy(void *unused,int fd)
{ (void)unused;int out=dup(fd);if(out<0)return -errno;owned++;return out; }
static void drop(void *unused,int fd)
{ (void)unused;assert(owned>0);assert(!close(fd));owned--; }
static int status(void *unused,int fd){(void)unused;(void)fd;return 1;}
static int io(void *unused,int fd,unsigned long req,void *arg)
{
 (void)unused;assert(fd==7&&req==DRM_IOCTL_MSM_VM_BIND);
 struct drm_msm_vm_bind *b=arg;assert(b->op.op==MSM_VM_BIND_OP_MAP);
 b->fence_fd=copy(NULL,STDOUT_FILENO);return b->fence_fd<0?b->fence_fd:0;
}
static int prepare(struct qti_vm_registry *r,struct drm_msm_gem_submit *req)
{
 struct qti_vm_sync_batch batch={0};struct drm_msm_gem_submit out;
 struct qti_vm_prepared_bo *prepared=calloc(req->nr_bos,sizeof(*prepared));assert(prepared);
 int rc=qti_vm_registry_prepare_sync_prepared(r,req,&batch,prepared);
 if(!rc)rc=qti_vm_sync_prepare_request(&batch,req,&out);
 if(!rc)assert(out.bos==req->bos&&out.nr_bos==req->nr_bos&&(out.flags&MSM_SUBMIT_NO_IMPLICIT));
 qti_vm_sync_finish(&batch);free(prepared);return rc;
}
int main(int argc,char **argv)
{
 if(argc!=2)return 2;
 char *end;unsigned long n=strtoul(argv[1],&end,10);if(*end||n<3||n>UINT32_MAX)return 2;
 struct qti_vm vm={.fd=7,.ioctl_fn=io,.va_start=0x100000000ULL,.va_size=(uint64_t)n*4096,
  .page_size=4096,.bind_queue=9,.enabled=true,.ready=true};
 const struct qti_vm_sync_ops ops={io,copy,drop};struct qti_vm_registry r;
 assert(!qti_vm_registry_init(&r,&vm,&ops,NULL,status));
 struct drm_msm_gem_submit_bo *bos=calloc(n,sizeof(*bos));assert(bos);
 for(unsigned long i=0;i<n;i++){
  struct qti_vm_mapping m;assert(!qti_vm_registry_add(&r,(uint32_t)i+1,4096,-1,QTI_VM_HANDLE_NEW,&m));
  bos[i]=(struct drm_msm_gem_submit_bo){MSM_SUBMIT_BO_READ|MSM_SUBMIT_BO_WRITE,(uint32_t)i+1,0};
 }
 uint32_t seed=1;
 for(unsigned long i=n-1;i>0;i--){seed=1664525*seed+1013904223;size_t j=seed%(i+1);
  struct drm_msm_gem_submit_bo tmp=bos[i];bos[i]=bos[j];bos[j]=tmp;}
 struct drm_msm_gem_submit req={.flags=MSM_PIPE_3D0,.bos=(uintptr_t)bos,.nr_bos=(uint32_t)n};
 sorts=0;int good=prepare(&r,&req);unsigned good_sorts=sorts;
 assert(!prepare(&r,&req)&&r.template_hits==1);
 bos[0].flags=MSM_SUBMIT_BO_READ;
 assert(!prepare(&r,&req)&&r.template_hits==1);
 req.queueid++;assert(!prepare(&r,&req)&&r.template_hits==1);
 assert(!prepare(&r,&req)&&r.template_hits==2);
 struct drm_msm_gem_submit_bo saved=bos[n-1];bos[n-1]=bos[0];
 sorts=0;int duplicate=prepare(&r,&req);unsigned duplicate_sorts=sorts;
 bos[n-1]=saved;sorts=0;int retry=prepare(&r,&req);unsigned retry_sorts=sorts;
 assert(good==0&&duplicate==-EINVAL&&retry==0&&!vm.lost);
 assert(!qti_vm_registry_finish(&r,true)&&owned==0);free(bos);
 printf("BOS=%lu GOOD_RC=%d SORTS=%u DUPLICATE_RC=%d DUPLICATE_SORTS=%u RETRY_RC=%d RETRY_SORTS=%u OWNED_FDS=%d\n",
  n,good,good_sorts,duplicate,duplicate_sorts,retry,retry_sorts,owned);
 return 0;
}
