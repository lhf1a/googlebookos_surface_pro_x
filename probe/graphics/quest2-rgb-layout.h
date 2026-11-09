/* Validate the donor RGB payload separately from its UBWC metadata prefix. */
#pragma once
#include "quest2-gralloc-utils-abi.h"
inline bool quest2_rgb_layout_valid(const gralloc::PlaneLayoutInfo &x,int format,
                                   uint32_t stride,uint32_t scan,uint32_t size,
                                   bool ubwc,uint32_t meta) {
    if(format!=1 && format!=2 && format!=5 && format!=43 && format!=22)return false;
    const int bpp=format==22?8:4;
    uint32_t components=(1u<<10)|(1u<<11)|(1u<<12);
    if(format!=2)components|=1u<<20;
    if(!stride || !scan || stride>INT32_MAX || scan>INT32_MAX ||
       x.component!=components || x.step!=bpp || x.stride!=int(stride) ||
       x.stride_bytes<=0 || uint64_t(x.stride_bytes)!=uint64_t(stride)*bpp ||
       x.scanlines!=int(scan) || x.h_subsampling || x.v_subsampling)return false;
    if(ubwc) {
        // The donor reports allocation size, not payload size, in RGB PlaneLayoutInfo.
        if(format==5 || !meta || (meta&4095) || meta>=size || x.offset!=meta || x.size!=size)
            return false;
    } else if(meta || x.offset || x.size>size)return false;
    return uint64_t(x.stride_bytes)*scan<=uint64_t(x.size)-meta;
}
