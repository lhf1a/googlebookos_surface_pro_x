/* Validate physical Y/Cr/Cb storage against the donor's planar YV12 layout. */
#pragma once
#include "quest2-gralloc-utils-abi.h"
inline bool quest2_yv12_layout_valid(const gralloc::PlaneLayoutInfo &y,
                                    const gralloc::PlaneLayoutInfo &cr,
                                    const gralloc::PlaneLayoutInfo &cb,
                                    uint32_t width,uint32_t height,
                                    uint32_t stride,uint32_t scan,uint32_t size) {
    if(!width || !height || (width&1) || (height&1) || stride<width || scan<height ||
       stride>INT32_MAX || scan>INT32_MAX || (stride&15) || (scan&1))return false;
    const uint64_t chroma_stride=(uint64_t(stride)/2+15)&~15ULL;
    const uint64_t y_size=uint64_t(stride)*scan,chroma_size=chroma_stride*(scan/2);
    if(y_size+2*chroma_size>size)return false;
    const gralloc::PlaneLayoutInfo *planes[]={&y,&cr,&cb};
    const uint32_t components[]={1,4,2};
    for(unsigned index=0;index<3;index++) {
        const auto &p=*planes[index];
        const uint64_t offset=index?y_size+(index-1)*chroma_size:0;
        if(p.component!=components[index] || p.h_subsampling!=(index?1u:0u) ||
           p.v_subsampling!=(index?1u:0u) || p.offset!=offset || p.step!=1 ||
           p.stride!=int32_t(stride) || p.stride_bytes!=int64_t(index?chroma_stride:stride) ||
           p.scanlines!=int32_t(index?scan/2:scan) || p.size!=(index?chroma_size:y_size))return false;
    }
    return true;
}
