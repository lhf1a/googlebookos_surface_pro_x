/* Explicit linear YUV metadata decoding for mapper3. Never flatten planes or
 * use a QTI private_handle layout to interpret minigbm handles. */
#pragma once
#include "quest2-mapper5.h"
namespace sq2 {
struct YuvPlane {
    struct Component {uint64_t type=0,offset=0,bits=0;} components[4];
    uint64_t count=0,offset=0,step=0,stride=0,width=0,height=0,size=0,hsub=0,vsub=0;
};
struct YuvInfo : BufferInfo {YuvPlane planes[3];uint64_t count=0;};
struct YuvLayout {uint64_t y=0,cb=0,cr=0,y_stride=0,c_stride=0,chroma_step=0;};
inline bool yuv_metadata(const native_handle_t *original,YuvInfo &out){
    AIMapper *m=mapper5();
    if(!m||!original||original->numFds<1)return false;
    struct Import {AIMapper *m;buffer_handle_t h=nullptr;~Import(){if(h)m->v5.freeBuffer(h);}} imported{m};
    if(m->v5.importBuffer(original,&imported.h)!=AIMAPPER_ERROR_NONE)return false;
    YuvInfo b;
    auto scalar=[&](int64_t key,void *dst,size_t n){Metadata r;return r.get(m,imported.h,key)&&r.read(dst,n)&&r.pos==r.size;};
    if(!scalar(1,&b.id,8)||!scalar(3,&b.width,8)||!scalar(4,&b.height,8)||!scalar(5,&b.layers,8)||
       !scalar(6,&b.format,4)||!scalar(7,&b.fourcc,4)||!scalar(8,&b.modifier,8)||
       !scalar(9,&b.usage,8)||!scalar(10,&b.size,8))return false;
    b.fd=original->data[0];struct stat st={};
    if(b.fd<0||fstat(b.fd,&st)||st.st_size<=0)return false;
    b.fd_size=static_cast<uint64_t>(st.st_size);
    if(!b.width||!b.height||b.layers!=1||b.modifier||!b.size||(b.usage&(1ull<<14)))return false;
    Metadata r;if(!r.get(m,imported.h,15)||!r.u64(b.count)||b.count<2||b.count>3)return false;
    for(uint64_t i=0;i<b.count;i++){
        auto &p=b.planes[i];if(!r.u64(p.count)||p.count<1||p.count>4)return false;
        for(uint64_t j=0;j<p.count;j++){
            auto &c=p.components[j];uint64_t len;
            const char name[]="android.hardware.graphics.common.PlaneLayoutComponentType";
            if(!r.u64(len)||len!=sizeof(name)-1||len>r.size-r.pos||memcmp(r.data+r.pos,name,len)||
               !r.read(nullptr,len)||!r.u64(c.type)||!r.u64(c.offset)||!r.u64(c.bits))return false;
        }
        if(!r.u64(p.offset)||!r.u64(p.step)||!r.u64(p.stride)||!r.u64(p.width)||
           !r.u64(p.height)||!r.u64(p.size)||!r.u64(p.hsub)||!r.u64(p.vsub))return false;
        if(!p.step||p.step%8||!p.stride||!p.width||!p.height||!p.hsub||!p.vsub||
           p.offset>b.fd_size||p.size>b.fd_size-p.offset||p.width>p.stride/(p.step/8)||
           p.height>p.size/p.stride)return false;
        for(uint64_t j=0;j<p.count;j++)if(p.components[j].offset%8||p.components[j].bits!=8||
            p.components[j].offset>p.step-8)return false;
    }
    if(r.pos!=r.size)return false;
    b.offset=b.planes[0].offset;b.stride=b.planes[0].stride;b.plane_size=b.planes[0].size;b.sample_bits=b.planes[0].step;
    out=b;return true;
}
inline bool yuv_info(const native_handle_t *handle,YuvInfo &b,YuvLayout &layout){
    if(!yuv_metadata(handle,b)||b.width>INT32_MAX||b.height>INT32_MAX)return false;
    bool planar=b.format==0x32315659u&&b.fourcc==0x32315659u&&b.count==3;
    bool nv12=b.format==35&&b.fourcc==0x3231564eu&&b.count==2;
    if(!planar&&!nv12)return false;
    unsigned found=0;uint64_t chroma_stride=0;
    for(unsigned i=0;i<b.count;i++){
        const auto &p=b.planes[i];const bool luma=i==0;
        if(p.hsub!=(luma?1u:2u)||p.vsub!=(luma?1u:2u)||p.width!=(b.width+p.hsub-1)/p.hsub||
           p.height!=(b.height+p.vsub-1)/p.vsub||p.stride>INT32_MAX||
           p.step!=(luma||planar?8u:16u)||p.count!=(luma||planar?1u:2u))return false;
        for(unsigned j=0;j<i;j++)if(p.offset<b.planes[j].offset+b.planes[j].size&&
            b.planes[j].offset<p.offset+p.size)return false;
        for(unsigned j=0;j<p.count;j++){
            const auto &c=p.components[j];
            if((c.type!=1&&c.type!=2&&c.type!=4)||(found&c.type)||luma!=(c.type==1))return false;
            found|=c.type;uint64_t offset=p.offset+c.offset/8;
            if(c.type==1){layout.y=offset;layout.y_stride=p.stride;}
            else{
                if(chroma_stride&&chroma_stride!=p.stride)return false;
                chroma_stride=p.stride;layout.c_stride=p.stride;layout.chroma_step=p.step/8;
                if(c.type==2)layout.cb=offset;else layout.cr=offset;
            }
        }
    }
    // AddressMappings currently returns base+offset. These supported layouts
    // must start at zero; no guessed base subtraction or fd-per-plane handling.
    return found==7&&layout.y==0&&b.offset==0&&b.plane_size%b.stride==0&&
        b.plane_size/b.stride<=INT32_MAX&&
        (planar?(layout.cr<layout.cb):(layout.cr==layout.cb+1));
}
}
