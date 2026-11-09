/* Native linear NV12/P010; strides below are bytes, ANW strides are pixels. */
#ifndef QUEST2_OMX_NATIVE_LAYOUT_H
#define QUEST2_OMX_NATIVE_LAYOUT_H
#include <stdint.h>
static inline unsigned quest2_omx_native_sample_bytes(unsigned format) {
    return format==0x7fa30c04U?1:format==0x7fa30c0aU?2:0;
}
static inline unsigned quest2_omx_native_v4l2_format(unsigned format) {
    return format==0x7fa30c04U?0x3231564eU:format==0x7fa30c0aU?0x30313050U:0;
}
static inline unsigned quest2_omx_native_gralloc_format(unsigned format) {
    return format==0x3231564eU?0x7fa30c04U:format==0x30313050U?0x7fa30c0aU:0;
}
struct native_handle;
#ifdef __cplusplus
extern "C" {
#endif
int quest2_omx_capture_layout_for_usage_v2(unsigned width,unsigned height,unsigned v4l2_format,
    uint64_t usage,unsigned minimum_size,unsigned *stride,unsigned *scan,unsigned *size);
/* Existing NV12 callers keep their original ABI; the paired bridge uses v2. */
int quest2_omx_capture_layout_for_usage(unsigned width,unsigned height,unsigned v4l2_format,
    uint64_t usage,unsigned *stride,unsigned *scan,unsigned *size);
/* Original fixed-usage entry is only for the private ImageReader experiment. */
int quest2_omx_capture_layout(unsigned width,unsigned height,unsigned v4l2_format,
    unsigned *stride,unsigned *scan,unsigned *size);
/* All expected fields belong to the negotiated session; format is the gralloc format. */
int quest2_omx_validate_native_buffer(const struct native_handle *raw,
    unsigned width,unsigned height,unsigned format,uint64_t usage,
    unsigned stride,unsigned scan,unsigned size);
#ifdef __cplusplus
}
#endif
#endif
