/* Pure checks shared by real backend allocation/import and host boundary tests. */
#pragma once
#include <stdint.h>
#include "quest2-omx-native-layout.h"
static inline bool quest2_omx_native_usage_valid(uint64_t usage) {
    const unsigned read=usage&15,write=(usage>>4)&15;
    // Allocator5's verified usage mask, excluding UBWC for these linear paths.
    const uint64_t supported=0x1b33ULL|0x2000ULL|0x400000ULL|0x20000000ULL;
    return !(usage&~supported) && (read==0 || read==2 || read==3) &&
        (write==0 || write==2 || write==3);
}
static inline bool quest2_omx_native_request_valid(unsigned w,unsigned h,uint64_t usage) {
    return w && h && w<=4096 && h<=4096 && !(w&1) && !(h&1) &&
        quest2_omx_native_usage_valid(usage);
}
template<class Geometry>
static bool quest2_omx_native_geometry_valid(const Geometry &g,unsigned w,unsigned h,
    unsigned format,uint64_t usage,const uint32_t *pitches,const uint32_t *offsets,unsigned planes) {
    const unsigned sample=quest2_omx_native_sample_bytes(format);
    if(!sample || !quest2_omx_native_request_valid(w,h,usage) || !pitches || !offsets || planes!=2 ||
       g.width!=w || g.height!=h || g.layers!=1 || unsigned(g.format)!=format ||
       g.usage!=usage || !g.yuv || g.ubwc || g.planar_yv12 || g.sample_bytes!=sample ||
       !g.stride || g.stride<w || g.scan<h || (g.scan&1) || !g.size ||
       g.size>16*1024*1024*sample)return false;
    const uint64_t row=uint64_t(g.stride)*sample,ysize=row*g.scan;
    const auto &y=g.plane;const auto &uv=g.chroma;
    return y.component==1 && uv.component==6 && !y.h_subsampling && !y.v_subsampling &&
        uv.h_subsampling==1 && uv.v_subsampling==1 && y.step==int(sample) && uv.step==int(2*sample) &&
        !y.offset && y.stride_bytes>0 && uint64_t(y.stride_bytes)==row &&
        y.scanlines==int(g.scan) && uint64_t(y.size)==ysize &&
        uint64_t(uv.offset)==ysize && uv.stride_bytes==y.stride_bytes &&
        uv.scanlines>=int(h/2) && uint64_t(uv.stride_bytes)*uv.scanlines<=uv.size &&
        uint64_t(uv.offset)+uv.size<=g.size &&
        pitches[0]==row && pitches[1]==row &&
        offsets[0]==0 && uint64_t(offsets[1])==ysize;
}
