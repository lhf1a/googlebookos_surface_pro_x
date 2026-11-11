/* SPDX-License-Identifier: GPL-2.0-only */
/* Actual msm_gem_fault; allocator, VMA and SMMU leaves are models. */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
typedef unsigned long vm_fault_t;typedef unsigned long pgoff_t;
#define PAGE_SHIFT 12
#define MSM_MADV_WILLNEED 0
#define MSM_SMMU_DOMAIN_UNSECURE 0
#define VM_FAULT_NOPAGE 1
#define VM_FAULT_SIGBUS 2
#define ERR_PTR(x) ((void *)(intptr_t)(x))
#define PTR_ERR(x) ((long)(intptr_t)(x))
#define IS_ERR(x) ((uintptr_t)(x)>=(uintptr_t)-4095)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
static int scenarios,lock_error,pages_error,inserts;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL fault scenario%d line%d: %s\n",scenarios,__LINE__,#x);exit(1);}}while(0)
struct mutex {int held;};
static int mutex_lock_interruptible(struct mutex *m){CHECK(!m->held);if(lock_error)return lock_error;m->held=1;return 0;}
static void mutex_unlock(struct mutex *m){CHECK(m->held);m->held=0;}
static int warn(bool value){return value;}
#define WARN_ON(x) warn(x)
struct drm_device {int unused;};struct drm_gem_object {struct drm_device *dev;};
struct msm_gem_address_space {int unused;};
struct msm_gem_object {struct drm_gem_object base;struct mutex lock;int madv;struct msm_gem_address_space *aspace;};
#define to_msm_bo(p) container_of(p,struct msm_gem_object,base)
struct vm_area_struct {void *vm_private_data;unsigned long vm_start;};
struct vm_fault {struct vm_area_struct *vma;unsigned long address;};
struct page {unsigned long pfn;};
static struct page pages[4]={{0x1234},{0x5678},{0x9abc},{0xdef0}};static struct page *page_ptrs[4]={&pages[0],&pages[1],&pages[2],&pages[3]};
static struct msm_gem_address_space aspace;
static struct msm_gem_address_space *msm_gem_smmu_address_space_get(struct drm_device *d,int domain){CHECK(d&&domain==0);return &aspace;}
static struct page **get_pages(struct drm_gem_object *o){CHECK(to_msm_bo(o)->lock.held);return pages_error?ERR_PTR(pages_error):page_ptrs;}
static vm_fault_t vmf_error(int r){CHECK(r<0);return (unsigned long)(-r)+10;}
static unsigned long page_to_pfn(struct page *p){return p->pfn;}
static void log_ignored(const char *f,...){(void)f;}
#define VERB(...) log_ignored(__VA_ARGS__)
static unsigned long expected_pfn;
/* Current 6.18 include/linux/mm.h: unsigned long pfn, no pfn_t wrapper. */
static vm_fault_t vmf_insert_mixed(struct vm_area_struct *v,unsigned long address,unsigned long pfn){CHECK(v&&address>=v->vm_start&&pfn==expected_pfn);inserts++;return 0;}
#include "actual-fault.h"
int main(void)
{
    struct drm_device dev={0};struct msm_gem_object obj={.base={&dev}};struct vm_area_struct vma={&obj.base,0x100000};struct vm_fault fault={&vma,0};
    for(int start=0;start<2;start++)for(int i=0;i<4;i++){scenarios++;vma.vm_start=start?0x200000:0x100000;fault.address=vma.vm_start+((unsigned long)i<<12)+4095;expected_pfn=pages[i].pfn;CHECK(msm_gem_fault(&fault)==0&&!obj.lock.held&&obj.aspace==&aspace);}
    scenarios++;int before=inserts;lock_error=-EINTR;CHECK(msm_gem_fault(&fault)==VM_FAULT_NOPAGE&&!obj.lock.held&&inserts==before);lock_error=0;
    scenarios++;obj.madv=1;CHECK(msm_gem_fault(&fault)==VM_FAULT_SIGBUS&&!obj.lock.held&&inserts==before);obj.madv=0;
    scenarios++;pages_error=-ENOMEM;CHECK(msm_gem_fault(&fault)==vmf_error(-ENOMEM)&&!obj.lock.held&&inserts==before);
    printf("PASS %d actual native fault PFN/API scenarios; VMA/pages/SMMU modeled\n",scenarios);return 0;
}
