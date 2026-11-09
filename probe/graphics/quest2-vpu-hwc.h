/* Diagnostic C boundary: completed decoder output -> HWC -> safe decoder requeue. */
#pragma once
#include <stdint.h>
#include <cutils/native_handle.h>
#ifdef __cplusplus
extern "C" {
#endif
int quest2_hwc_video_present(unsigned slot,const native_handle_t *handle,uint64_t id,
                            unsigned width,unsigned height,int64_t pts,int *released_slot);
int quest2_hwc_video_flush(int *released_slot);
void quest2_hwc_video_finish(void);
void quest2_capture_vpu_event(unsigned operation,unsigned queue_index,int dmafd,
                             uint32_t bytes,int64_t pts,uint32_t flags);
#ifdef __cplusplus
}
#endif
