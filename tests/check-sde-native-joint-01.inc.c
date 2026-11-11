/* Host-only model/fixture; see check-sde-native-README.md. */

typedef uint64_t u64;
#define PAGE_SIZE 4096
#define check_mul_overflow __builtin_mul_overflow
#define check_add_overflow __builtin_add_overflow
struct kref {int refs;};static void kref_get(struct kref*r){r->refs++;}
struct msm_gem_vma {uint64_t iova;bool mapped;};
struct msm_gem_address_space {struct msm_mmu*mmu;bool domain_attached;struct kref kref;};
static void msm_gem_address_space_put(struct msm_gem_address_space*a){a->kref.refs--;}
