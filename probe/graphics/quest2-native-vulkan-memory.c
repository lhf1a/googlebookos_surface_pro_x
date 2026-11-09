/* Preserve v77 HOST_CACHED policy with the native GSL in this same ELF namespace.
 * No handles are inspected or translated. Explicit cache modes remain exact. */
#include "quest2-native-vulkan-memory.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
static _Thread_local struct Sq2VulkanMemoryScope current;
extern int gsl_memory_alloc_pure_64(uint64_t, uint64_t, void *);
struct Sq2VulkanMemoryScope sq2_native_vulkan_memory_scope(struct Sq2VulkanMemoryScope value) {
    struct Sq2VulkanMemoryScope previous=current;
    current=value;
    return previous;
}
int sq2_memory_alloc_pure_64(uint64_t size,uint64_t flags,void *out) {
    uint64_t effective=flags;
    if(current.enabled && !(flags&UINT64_C(0x0c000000)))effective|=UINT64_C(0x0c000000);
    int result=gsl_memory_alloc_pure_64(size,effective,out);
    if(getenv("SQ2_VULKAN_MEMORY_TRACE"))
        fprintf(stderr,"QTI_GSL_ALLOC serial=%" PRIu64 " type=%u properties=%x cached_scope=%u size=%" PRIu64
                " flags=%" PRIx64 " effective=%" PRIx64 " result=%d\n",
                current.serial,current.memory_type,current.memory_flags,current.enabled,size,flags,effective,result);
    return result;
}
