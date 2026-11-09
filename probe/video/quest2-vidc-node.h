/* Process-local node binding; tokens are never descriptor numbers or reused. */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* begin returns 0 on failure; nested scopes fail without replacing the outer.
 * end always clears this thread's scope, and requires exactly one live open.
 * No persistent token reference: closing the last tracked alias invalidates it. */
uint64_t quest2_vidc_node_begin(void);
int quest2_vidc_node_end(uint64_t token);
/* int APIs return 0 or -1 with errno. Usage is the negotiated allocator usage.
 * Native with unknown usage is recorded but cannot negotiate a capture layout.
 * Repeated identical configuration is allowed while busy; changes are not. */
int quest2_vidc_node_configure(uint64_t token, int native, uint64_t usage, int usage_valid);
struct quest2_vidc_node_layout {
    uint32_t width, height, format, stride, scan, size;
    uint64_t usage;
};
/* EAGAIN only before the first SOURCE_CHANGE on a configured native node.
 * Reconfiguration still pending is EBUSY, or the real layout failure. */
int quest2_vidc_node_get_layout(uint64_t token, struct quest2_vidc_node_layout *layout);
#ifdef __cplusplus
}
#endif
