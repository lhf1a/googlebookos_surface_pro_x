/* Read-only metadata check for the optional exact RGBA texture relay probe.
 * The temporary mapper import is freed inside color_info; AHB ownership stays
 * with the C probe. No donor handle, pixel data, or system state is modified. */
#include <android/hardware_buffer.h>
#include "quest2-mapper5.h"

extern "C" const native_handle_t *AHardwareBuffer_getNativeHandle(const AHardwareBuffer *);

extern "C" int sq2_readpixels_linear_ahb_info(const AHardwareBuffer *buffer,
                                              unsigned width, unsigned height,
                                              uint64_t *stride, uint64_t *plane_size) {
    sq2::BufferInfo b;
    if (!buffer || !stride || !plane_size ||
        !sq2::color_info(AHardwareBuffer_getNativeHandle(buffer), b) ||
        b.format != AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM ||
        b.fourcc != 0x34324241u || b.sample_bits != 32 ||
        b.width != width || b.height != height || b.layers != 1 ||
        (b.usage & AHARDWAREBUFFER_USAGE_PROTECTED_CONTENT) ||
        b.stride % 4 || b.offset % 4 ||
        b.stride < static_cast<uint64_t>(width) * 4 ||
        height > b.plane_size / b.stride || b.stride > UINT32_MAX) return 0;
    *stride = b.stride;
    *plane_size = b.plane_size;
    return 1;
}
