#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include "../probe/graphics/quest2-gralloc-utils-abi.h"
#include "../probe/video/quest2-omx-native-layout-check.h"
struct Geometry {
    unsigned width=320,height=240,layers=1,stride=512,scan=512,size=393216;
    int format=0x7fa30c04;
    uint64_t usage=0x20400900;
    bool yuv=true,ubwc=false,planar_yv12=false;
    unsigned sample_bytes=1;
    gralloc::PlaneLayoutInfo plane{1,0,0,0,1,512,512,512,262144};
    gralloc::PlaneLayoutInfo chroma{6,1,1,262144,2,512,512,256,131072};
};
int main() {
    const Geometry good{};
    const uint32_t pitches[4]={512,512},offsets[4]={0,262144};
    auto valid=[&](const Geometry &g) {
        return quest2_omx_native_geometry_valid(g,320,240,0x7fa30c04,0x20400900,pitches,offsets,2);
    };
    assert(valid(good));
    for(uint64_t usage:{0x20400900ULL,0x20400903ULL,0x20400b03ULL})
        assert(quest2_omx_native_usage_valid(usage));
    const uint64_t supported=0x20403b33ULL;
    for(unsigned bit=0;bit<64;bit++)
        if(!((1ULL<<bit)&supported))
            assert(!quest2_omx_native_usage_valid(good.usage|(1ULL<<bit)));
    for(uint64_t cpu:{0ULL,2ULL,3ULL,0x20ULL,0x30ULL,0x22ULL,0x33ULL})
        assert(quest2_omx_native_usage_valid(0x20400900|cpu));
    for(uint64_t bad:{1ULL,4ULL,0x10ULL,0x40ULL,0x4000ULL,0x10000000ULL,
                       0x40000000ULL,0x80000000ULL,1ULL<<49,1ULL<<58})
        assert(!quest2_omx_native_usage_valid(0x20400900|bad));
    assert(!quest2_omx_native_request_valid(0,240,good.usage));
    assert(!quest2_omx_native_request_valid(321,240,good.usage));
    assert(!quest2_omx_native_request_valid(320,241,good.usage));
    assert(!quest2_omx_native_request_valid(4098,240,good.usage));
#define REJECT(change) do { auto g=good;change;assert(!valid(g)); } while(0)
    REJECT(g.width=318);REJECT(g.height=238);REJECT(g.layers=2);
    REJECT(g.format=0x36);REJECT(g.usage|=0x10000);REJECT(g.usage=0x20400100);
    REJECT(g.yuv=false);REJECT(g.ubwc=true);REJECT(g.planar_yv12=true);
    REJECT(g.sample_bytes=2);REJECT(g.stride=320);REJECT(g.scan=511);
    REJECT(g.size=393215);REJECT(g.size=UINT32_MAX);
    REJECT(g.plane.offset=1);REJECT(g.plane.stride_bytes=-1);
    REJECT(g.plane.size=262143);REJECT(g.chroma.offset=262145);
    REJECT(g.chroma.stride_bytes=256);REJECT(g.chroma.scanlines=119);
    REJECT(g.chroma.size=131071);REJECT(g.chroma.step=1);
    uint32_t badpitch[4]={512,256},badoffset[4]={0,262145};
    assert(!quest2_omx_native_geometry_valid(good,320,240,0x7fa30c04,good.usage,badpitch,offsets,2));
    assert(!quest2_omx_native_geometry_valid(good,320,240,0x7fa30c04,good.usage,pitches,badoffset,2));
    assert(!quest2_omx_native_geometry_valid(good,320,240,0x7fa30c04,good.usage,pitches,offsets,1));
    assert(!quest2_omx_native_geometry_valid(good,320,240,0x7fa30c04,good.usage,nullptr,offsets,2));
    assert(quest2_omx_native_sample_bytes(0x7fa30c04)==1);
    assert(quest2_omx_native_sample_bytes(0x7fa30c0a)==2);
    assert(!quest2_omx_native_sample_bytes(0x36));
    assert(quest2_omx_native_v4l2_format(0x7fa30c0a)==0x30313050);
    assert(quest2_omx_native_gralloc_format(0x30313050)==0x7fa30c0a);
    auto p010=good;p010.format=0x7fa30c0a;p010.sample_bytes=2;p010.size*=2;
    p010.plane.step=2;p010.plane.stride_bytes*=2;p010.plane.size*=2;
    p010.chroma.step=4;p010.chroma.stride_bytes*=2;p010.chroma.size*=2;p010.chroma.offset*=2;
    const uint32_t p010_pitch[4]={1024,1024},p010_offset[4]={0,524288};
    auto valid_p010=[&](const Geometry& g){
        return quest2_omx_native_geometry_valid(g,320,240,0x7fa30c0a,g.usage,p010_pitch,p010_offset,2);
    };
    assert(valid_p010(p010));
    assert(!valid(p010));
    {auto g=p010;g.plane.step=1;assert(!valid_p010(g));}
    {auto g=p010;g.chroma.step=2;assert(!valid_p010(g));}
    {auto g=p010;g.sample_bytes=1;assert(!valid_p010(g));}
    {auto g=p010;g.format=0x36;assert(!valid_p010(g));}
    {auto g=p010;g.chroma.offset/=2;assert(!valid_p010(g));}
    {auto g=p010;g.plane.stride_bytes/=2;assert(!valid_p010(g));}
    {auto g=p010;g.size=UINT32_MAX;assert(!valid_p010(g));}
    assert(!quest2_omx_native_geometry_valid(p010,320,240,0x7fa30c0a,p010.usage,pitches,p010_offset,2));
    assert(!quest2_omx_native_geometry_valid(p010,320,240,0x7fa30c0a,p010.usage,p010_pitch,offsets,2));
    puts("OMX native layout: NV12/P010, byte/pixel stride, exact usage and boundaries passed");
}
