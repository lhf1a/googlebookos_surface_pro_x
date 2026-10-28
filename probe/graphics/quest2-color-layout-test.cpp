#include <cstdio>
#include <initializer_list>
#include "quest2-mapper5.h"
int main() {
    sq2::BufferInfo b;b.format=22;b.fourcc=0x48344241;b.sample_bits=64;b.component_count=4;
    b.width=b.plane_width=13;b.height=b.plane_height=7;b.layers=1;b.hsub=b.vsub=1;
    b.stride=128;b.plane_size=b.fd_size=b.size=896;
    const uint64_t types[]={1024,2048,4096,1073741824};
    for(unsigned i=0;i<4;i++)b.components[i]={types[i],i*16u,16};
    unsigned checks=0,failures=0;
    auto check=[&](bool result){++checks;if(!result)++failures;};
    check(sq2::linear_rgba_layout(b));
    {auto shuffled=b;auto saved=shuffled.components[0];shuffled.components[0]=shuffled.components[3];
     shuffled.components[3]=saved;check(sq2::linear_rgba_layout(shuffled));}
#define DENY(change) {auto bad=b;change;check(!sq2::linear_rgba_layout(bad));}
    DENY(bad.component_count=3);
    for(unsigned i=0;i<4;i++) {
        DENY(bad.components[i].bits=8);
        DENY(bad.components[i].offset+=1);
        DENY(bad.components[i].type=0);
    }
    DENY(bad.components[3]=bad.components[0]);
    DENY(bad.sample_bits=32);DENY(bad.sample_bits=0);DENY(bad.fourcc=0x34324241);
    DENY(bad.format=2);DENY(bad.format=5);DENY(bad.modifier=1);DENY(bad.layers=2);
    DENY(bad.usage=1ull<<14);DENY(bad.stride=0);DENY(bad.stride=127);
    DENY(bad.offset=1);DENY(bad.width=17;bad.plane_width=17);
    DENY(bad.plane_width=14);DENY(bad.plane_height=8);DENY(bad.hsub=2);DENY(bad.vsub=2);
    DENY(bad.plane_size=128);
    auto rgba=b;rgba.format=1;rgba.fourcc=0x34324241;rgba.sample_bits=32;
    for(unsigned i=0;i<4;i++)rgba.components[i]={types[i],i*8u,8};
    check(sq2::linear_rgba_layout(rgba));
    for(unsigned format:{2u,5u}) {
        b=rgba;b.format=format;b.fourcc=format==2?0x34324258:0x34325241;
        b.component_count=format==2?3:4;
        if(format==5){b.components[0].offset=16;b.components[2].offset=0;}
        check(sq2::linear_rgba_layout(b));
        DENY(bad.fourcc=0x34324241);
        DENY(bad.component_count=format==2?4:3);
        for(unsigned i=0;i<b.component_count;i++) {
            DENY(bad.components[i].bits=16);
            DENY(bad.components[i].offset+=8);
            DENY(bad.components[i].type=0);
        }
        DENY(bad.components[1]=bad.components[0]);
        DENY(bad.sample_bits=24);DENY(bad.modifier=1);DENY(bad.layers=2);
        DENY(bad.usage=1ull<<14);DENY(bad.hsub=2);DENY(bad.stride=51);
    }
    printf("COLOR_LAYOUT_TEST checks=%u failed=%u\n",checks,failures);return failures?1:0;
}
