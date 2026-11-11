/* Host-only model/fixture; see check-sde-native-README.md. */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#define MSM_SMMU_DOMAIN_MAX 4
#define MSM_SMMU_DOMAIN_UNSECURE 0
#define DOMAIN_ATTR_EARLY_MAP 0
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define IS_ERR(p) ((uintptr_t)(p)>(uintptr_t)-4096)
#define ERR_PTR(x) ((void *)(intptr_t)(x))
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define SDE_DEBUG(...) ((void)0)
#define SDE_ERROR(...) ((void)0)
struct msm_mmu;
static int create_err,attach_err,aspace_err,destroyed,updates,detaches,update_err;
struct funcs {int(*attach)(struct msm_mmu*,const char*const*,int);int(*detach)(struct msm_mmu*,const char*const*,int);void(*destroy)(struct msm_mmu*);bool(*is_domain_secure)(struct msm_mmu*);int(*set_attribute)(struct msm_mmu*,int,void*);};
struct msm_mmu {struct funcs *funcs;};
struct msm_gem_address_space {struct msm_mmu*mmu;bool domain_attached;};
struct drm_device {void*dev;};
struct sde_kms {struct drm_device*dev;struct {int num_splash_regions, num_splash_displays;}splash_data;struct msm_gem_address_space *aspace[4];struct {struct msm_gem_address_space*aspace;}base;};
static int attach(struct msm_mmu*m,const char*const*n,int c){return attach_err;}
static int detach(struct msm_mmu*m,const char*const*n,int c){detaches++;return -EOPNOTSUPP;}
static void destroy(struct msm_mmu*m){destroyed++;}
static bool secure(struct msm_mmu*m){return false;}
static int attr(struct msm_mmu*m,int a,void*v){return 0;}
static struct funcs f={attach,detach,destroy,secure,attr};static struct msm_mmu mmu={&f};static struct msm_gem_address_space aspace={&mmu,false};
static struct msm_mmu *msm_smmu_new(void*d,int i){return create_err?ERR_PTR(create_err):&mmu;}
static struct msm_gem_address_space*msm_gem_smmu_address_space_create(void*d,struct msm_mmu*m,const char*n){return aspace_err?ERR_PTR(aspace_err):&aspace;}
static void msm_gem_address_space_put(struct msm_gem_address_space*a){destroy(a->mmu);}
static int msm_gem_aspace_domain_attach_detach_update(struct msm_gem_address_space*a,bool detach){updates++;return update_err;}
static int _sde_kms_map_all_splash_regions(struct sde_kms*k){return -EOPNOTSUPP;}
static void _sde_kms_unmap_all_splash_regions(struct sde_kms*k){}
static const char *iommu_ports[]={"mdp_0"};
