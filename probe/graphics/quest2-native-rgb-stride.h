#ifndef QUEST2_NATIVE_RGB_STRIDE_H
#define QUEST2_NATIVE_RGB_STRIDE_H
#include <stdint.h>
#include <limits.h>
static inline int32_t quest2_rgb_bpp(int32_t format) {
    switch(format) {
    case 1: case 2: case 5: case 43:return 4;
    case 3:return 3;
    case 4:return 2;
    case 22:return 8;
    default:return 0;
    }
}
static inline int32_t quest2_rgb_byte_stride(int32_t format,int32_t width,int32_t height,
                                            int32_t size,int32_t bpp,int32_t row) {
    int32_t expected=quest2_rgb_bpp(format);
    if(!expected || expected!=bpp || width<=0 || height<=0 || size<=0 ||
       width>INT32_MAX/expected || (int64_t)width*height*expected>size)return row;
    return row==width?width*expected:row;
}
#endif
