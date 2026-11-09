/* Standard AHB ownership and mapper5 layout for the native decoder diagnostic. */
#pragma once
#include <stdint.h>
#include <cutils/native_handle.h>
#include <android/hardware_buffer.h>
struct quest2_video_buffer {
    AHardwareBuffer *ahb;
    const native_handle_t *handle;
    uint64_t capacity,usage;
    uint32_t stride,ystride,cstride,uv;
};
#ifdef __cplusplus
extern "C" {
#endif
int quest2_video_buffer_allocate(unsigned,unsigned,int,uint64_t,struct quest2_video_buffer *);
/* Retain an existing consumer AHB and inspect its actual mapper5 layout. */
int quest2_video_buffer_import(AHardwareBuffer *,struct quest2_video_buffer *);
void quest2_video_buffer_release(struct quest2_video_buffer *);
void quest2_video_buffer_check_maps(void);
#ifdef __cplusplus
}
#endif
