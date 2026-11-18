/* Preserve v77 HOST_CACHED policy with the native GSL in this same ELF namespace.
 * No handles are inspected or translated. Explicit cache modes remain exact. */
#include "quest2-native-vulkan-memory.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
static _Thread_local struct Sq2VulkanMemoryScope current;
static _Thread_local uint64_t dedicated_bytes;
static _Thread_local unsigned share_depth;
void sq2_native_vulkan_share_guard(int enter){
    if(enter)++share_depth;else --share_depth;
}
extern unsigned qti_vm_swap_gsl_scope(unsigned) __attribute__((weak));
/* Optional: an unadapted GSL has no VM_BIND bridge in its dependency graph. */
extern uint64_t qti_vm_swap_private_gem_size(uint64_t) __attribute__((weak));
uint64_t sq2_native_vulkan_dedicated_scope(uint64_t bytes) {
    uint64_t previous=dedicated_bytes;dedicated_bytes=bytes;return previous;
}
extern int gsl_memory_alloc_pure_64(uint64_t, uint64_t, void *);
struct Sq2VulkanMemoryScope sq2_native_vulkan_memory_scope(struct Sq2VulkanMemoryScope value) {
    struct Sq2VulkanMemoryScope previous=current;
    current=value;
    return previous;
}
int sq2_memory_alloc_pure_64(uint64_t size,uint64_t flags,void *out) {
    uint64_t effective=flags;
    if(current.enabled && !(flags&UINT64_C(0x0c000000)))effective|=UINT64_C(0x0c000000);
    uint64_t expected=dedicated_bytes,previous=0;
    dedicated_bytes=0; /* one direct GSL allocation, not subsequent side allocations */
    if(qti_vm_swap_private_gem_size)
        previous=qti_vm_swap_private_gem_size(size && size==expected?size:0);
    unsigned old_role=0;
    if(qti_vm_swap_gsl_scope)old_role=qti_vm_swap_gsl_scope(share_depth?2u:1u);
    int result=gsl_memory_alloc_pure_64(size,effective,out);
    if(qti_vm_swap_gsl_scope)qti_vm_swap_gsl_scope(old_role);
    if(qti_vm_swap_private_gem_size)qti_vm_swap_private_gem_size(previous);
    if(getenv("SQ2_VULKAN_MEMORY_TRACE"))
        fprintf(stderr,"QTI_GSL_ALLOC serial=%" PRIu64 " type=%u properties=%x cached_scope=%u size=%" PRIu64
                " flags=%" PRIx64 " effective=%" PRIx64 " result=%d\n",
                current.serial,current.memory_type,current.memory_flags,current.enabled,size,flags,effective,result);
    return result;
}
