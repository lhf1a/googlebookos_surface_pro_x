/* Host-only model/fixture; see check-sde-native-README.md. */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
#include <limits.h>
typedef uint32_t u32; typedef uint64_t dma_addr_t;
#define EPROBE_DEFER 517
#define GFP_KERNEL 0
#define __init
#define __exit
#define DMA_TO_DEVICE 1
#define DMA_FROM_DEVICE 2
#define DMA_BIDIRECTIONAL 0
#define DMA_ATTR_IOMMU_USE_LLC_NWA 1
#define DRM_ERROR(...) ((void)0)
#define DRM_DEBUG(...) ((void)0)
#define SDE_EVT32(...) ((void)0)
#define WARN_ON(x) ({ bool _warn = !!(x); _warn; })
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define ERR_PTR(x) ((void *)(intptr_t)(x))
#define IS_ERR(p) ((uintptr_t)(p)>(uintptr_t)-4096)
#define PTR_ERR(p) ((int)(intptr_t)(p))
struct list_head { struct list_head *next,*prev; };
#define INIT_LIST_HEAD(h) ((h)->next=(h)->prev=(h))
static int list_empty(struct list_head *h){return h->next==h;}
static void list_add_tail(struct list_head *n,struct list_head *h){n->prev=h->prev;n->next=h;h->prev->next=n;h->prev=n;}
static void list_del(struct list_head *n){n->next->prev=n->prev;n->prev->next=n->next;}
#define list_for_each_entry(p,h,m) for(p=container_of((h)->next,__typeof__(*p),m); &p->m!=(h); p=container_of(p->m.next,__typeof__(*p),m))
struct mutex {int x;};
static void mutex_init(struct mutex*m){} static void mutex_lock(struct mutex*m){} static void mutex_unlock(struct mutex*m){}
#define IOMMU_COOKIE_DMA_IOVA 1
struct iommu_domain {int type; void *iova_cookie; int cookie_type;};
struct device_node {int mdss, iommus;};
struct device {struct device *parent;struct device_node *of_node;struct iommu_domain *domain;void *driver;uint64_t *dma_mask;int refs;};
struct platform_device {struct device dev;};
static int unregisters,device_puts,maps,unmaps,map_error,map_nents=1,last_unmap_nents,alloc_fail;
static struct device *get_device(struct device*d){d->refs++;return d;}
static void put_device(struct device*d){d->refs--;device_puts++;}
#define to_platform_device(d) ((struct platform_device *)(d))
static void platform_device_unregister(struct platform_device*p){unregisters++;}
static void *kzalloc(size_t n,int g){if(alloc_fail)return NULL;return calloc(1,n);}
#define kfree free
static int of_device_is_compatible(struct device_node*n,const char*s){return n->mdss;}
static void *of_find_property(struct device_node*n,const char*s,void*x){return n->iommus?n:NULL;}
static struct iommu_domain *iommu_get_domain_for_dev(struct device*d){return d->domain;}
static bool iommu_is_dma_domain(struct iommu_domain*d){return d->type==2;}
struct scatterlist {dma_addr_t dma_address;unsigned int dma_length;};
struct sg_table {struct scatterlist *sgl;unsigned int nents,orig_nents;};
#define sg_dma_address(s) ((s)->dma_address)
#define sg_dma_len(s) ((s)->dma_length)
#define for_each_sg(s,sg,n,i) for(i=0,sg=s;i<n;i++,sg++)
static int dma_map_sgtable(struct device*d,struct sg_table*s,int dir,unsigned long attrs){maps++;if(map_error)return map_error;s->nents=map_nents;return 0;}
static void dma_unmap_sgtable(struct device*d,struct sg_table*s,int dir,unsigned long attrs){unmaps++;last_unmap_nents=s->orig_nents;}
static int dma_map_sg_attrs(struct device*d,struct scatterlist*s,int n,int dir,unsigned long attrs){maps++;return map_error?0:map_nents;}
static void dma_unmap_sg(struct device*d,struct scatterlist*s,int n,int dir){unmaps++;last_unmap_nents=n;}
enum msm_mmu_domain_type {MSM_SMMU_DOMAIN_UNSECURE,MSM_SMMU_DOMAIN_NRT_UNSECURE,MSM_SMMU_DOMAIN_SECURE,MSM_SMMU_DOMAIN_NRT_SECURE,MSM_SMMU_DOMAIN_MAX};
struct msm_mmu;
struct msm_mmu_funcs {int(*attach)(struct msm_mmu*,const char*const*,int);int(*detach)(struct msm_mmu*,const char*const*,int);int(*map)(struct msm_mmu*,uint64_t,struct sg_table*,unsigned,int);int(*unmap)(struct msm_mmu*,uint64_t,struct sg_table*,unsigned);int(*map_dma_buf)(struct msm_mmu*,struct sg_table*,int,u32);void(*unmap_dma_buf)(struct msm_mmu*,struct sg_table*,int,u32);void(*destroy)(struct msm_mmu*);bool(*is_domain_secure)(struct msm_mmu*);int(*one_to_one_map)(struct msm_mmu*,uint32_t,uint32_t,uint32_t,int);int(*one_to_one_unmap)(struct msm_mmu*,uint32_t,uint32_t);struct device*(*get_dev)(struct msm_mmu*);};
struct msm_mmu {struct device*dev;const struct msm_mmu_funcs*funcs;};
static void msm_mmu_init(struct msm_mmu*m,struct device*d,const struct msm_mmu_funcs*f){m->dev=d;m->funcs=f;}
