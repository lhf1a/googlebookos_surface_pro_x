/* OMX U32 consumer-usage wire contract and the pinned ACodec/libui transform. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
static inline int quest2_omx_decode_consumer_usage(const void *input,size_t size,uint32_t *usage) {
    uint32_t wire[4];
    if(!input||!usage||size!=sizeof(wire))return -EINVAL;
    memcpy(wire,input,sizeof(wire));
    /* ACodec InitOMXParams is 1.0; the existing native diagnostic uses 1.1. */
    if(wire[0]!=sizeof(wire)||(wire[1]!=1&&wire[1]!=0x101)||wire[2]!=1)return -EINVAL;
    *usage=wire[3];return 0;
}
static inline uint64_t quest2_omx_requested_usage(uint32_t consumer,uint32_t producer) {
    return (uint64_t)consumer|(uint64_t)producer|0x402900ULL;
}
static inline uint64_t quest2_omx_allocation_usage(uint32_t consumer,uint32_t producer) {
    return quest2_omx_requested_usage(consumer,producer)&~0x2400ULL;
}
/* NV12 byte stride is also the maximum allocation pixel width. */
static inline int quest2_omx_allocation_fits_capture(uint32_t visible_width,uint32_t visible_height,
    uint32_t allocation_width,uint32_t allocation_height,uint32_t stride,uint32_t scan,
    uint32_t port_bytes,uint32_t allocation_bytes) {
    return visible_width&&visible_height&&port_bytes&&
        visible_width<=allocation_width&&visible_height<=allocation_height&&
        allocation_width<=stride&&allocation_height<=scan&&port_bytes<=allocation_bytes;
}
