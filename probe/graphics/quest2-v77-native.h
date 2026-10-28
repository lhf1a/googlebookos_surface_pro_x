/* Private adapter ABI, not an Android or vendor native-handle definition. */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct Sq2V77NativeInfo {
    uint32_t width, height, layers, format, stride;
    uint64_t size;
};
int sq2_v77_native_prepare(const void *original, const void **view, void **lease,
                          struct Sq2V77NativeInfo *info);
void sq2_v77_native_finish(void *lease);
unsigned sq2_v77_native_stats(uint64_t *bytes);
#ifdef SQ2_VK_BGRA_EXPORT
struct Sq2V77AllocationInfo { uint32_t format, width, height; };
struct Sq2V77AllocationInfo sq2_v77_allocation_scope(struct Sq2V77AllocationInfo info);
#endif
#ifdef __cplusplus
}
#endif
