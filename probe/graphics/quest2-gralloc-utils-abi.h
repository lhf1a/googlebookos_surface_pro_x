/* QTI exported utility ABI from sm8150/display 484ca557.../gralloc/gr_utils.h.
 * This is the public function-call ABI, not private_handle_t memory layout.
 * Versioned donor inputs must be verified by the builder. */
#pragma once
#include <stdint.h>
#include <stddef.h>
struct private_handle_t;
namespace gralloc {
struct BufferInfo { int width,height,format,layer_count;uint64_t usage; };
struct PlaneLayoutInfo {
    uint32_t component,h_subsampling,v_subsampling,offset;
    int32_t step,stride,stride_bytes,scanlines;
    uint32_t size;
};
int GetBufferSizeAndDimensions(const BufferInfo &,unsigned *,unsigned *,unsigned *);
int GetYUVPlaneInfo(const BufferInfo &,int32_t,int32_t,int32_t,int32_t,int *,PlaneLayoutInfo *);
void GetRGBPlaneInfo(const BufferInfo &,int32_t,int32_t,int32_t,int32_t,int *,PlaneLayoutInfo *);
bool IsUBwcEnabled(int,uint64_t);
unsigned GetRgbMetaSize(int,uint32_t,uint32_t,uint64_t);
int GetBufferLayout(private_handle_t *,uint32_t *,uint32_t *,uint32_t *);
}
static_assert(sizeof(gralloc::BufferInfo)==24 && offsetof(gralloc::BufferInfo,usage)==16);
static_assert(sizeof(gralloc::PlaneLayoutInfo)==36);
