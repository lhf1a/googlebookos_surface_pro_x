#include "../probe/camera/spx-isp.h"
#include <cassert>
#include <cmath>
#include <stdexcept>
int main() {
    for (bool front : {false,true}) {
        spx::Frame f{8,4,16,0,0,front,std::vector<uint8_t>(64,0xaa)};
        for (unsigned y=0;y<4;++y) for (unsigned x=0;x<8;x+=4) {
            auto* p=f.raw10.data()+y*16+x/4*5; p[4]=0;
            for(unsigned k=0;k<4;++k) {
                bool red=front ? (y%2 && (x+k)%2) : (!(y%2) && (x+k)%2);
                bool blue=front ? (!(y%2) && !((x+k)%2)) : (y%2 && !((x+k)%2));
                p[k]=red?128:blue?32:64;
            }
        }
        spx::Isp isp;
        auto im=isp.process(f,4,2);
        assert(im.rgb.size()==24 && im.rgb[0]>im.rgb[1] && im.rgb[1]>im.rgb[2]);
        assert(std::abs(im.linearMean - (288.f-(front?16.f:64.f))/(1023.f-(front?16.f:64.f)))<.001f);
        for(size_t i=3;i<im.rgb.size();++i) assert(im.rgb[i]==im.rgb[i%3]);
        isp.prepare(f,true);
        auto full=isp.render(8,4);
        assert(full.width==8 && full.height==4 && full.rgb.size()==96);
        for(size_t i=3;i<full.rgb.size();++i)assert(full.rgb[i]==full.rgb[i%3]);
        assert(isp.render(8,4).rgb==full.rgb); // Rendering must not advance AWB again.
        assert(isp.render(4,2).linearMean==full.linearMean);
        // A green photosite must remain distinct at native resolution.
        unsigned gx=front?1:0;
        f.raw10[gx]=160;
        isp.prepare(f,true);
        auto detail=isp.render(8,4);
        assert(detail.rgb[gx*3+1]>detail.rgb[(gx+2)*3+1]);
        f.raw10.resize(10);
        bool caught=false; try {isp.process(f,4,2);} catch(const std::runtime_error&){caught=true;}
        assert(caught);
    }
}
