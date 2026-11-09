#ifndef SQ2_NATIVE_VULKAN_MEMORY_H
#define SQ2_NATIVE_VULKAN_MEMORY_H
#include <stdint.h>
struct Sq2VulkanMemoryScope {
    unsigned enabled;
    uint32_t memory_type, memory_flags;
    uint64_t serial;
};
struct Sq2VulkanMemoryScope sq2_native_vulkan_memory_scope(struct Sq2VulkanMemoryScope value);
int sq2_memory_alloc_pure_64(uint64_t size, uint64_t flags, void *out);
#endif
