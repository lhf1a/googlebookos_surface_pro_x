// SPDX-License-Identifier: GPL-2.0-only
#include "spx-isp.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
namespace spx {
Image Isp::process(const Frame& f, unsigned w, unsigned h) {
    prepare(f,w>f.width/2 || h>f.height/2);
    return render(w,h);
}
void Isp::prepare(const Frame& f,bool fullResolution) {
    if (f.width < 4 || f.height < 2 || f.width % 4 ||
        f.stride < f.width / 4 * 5 || f.raw10.size() < size_t(f.stride) * f.height)
        throw std::runtime_error("invalid RAW10 image");
    auto sample = [&](int x, int y) {
        // Mirror at the image edge to preserve the Bayer color parity.
        if(x<0)x=1;else if(x>=int(f.width))x=f.width-2;
        if(y<0)y=1;else if(y>=int(f.height))y=f.height-2;
        const auto* p = f.raw10.data() + y * f.stride + (x / 4) * 5;
        return float((p[x % 4] << 2) | ((p[4] >> ((x % 4) * 2)) & 3));
    };
    bw = fullResolution?f.width:f.width/2; bh = fullResolution?f.height:f.height/2;
    cells.resize(size_t(bw)*bh);
    double sums[3]{};
    for (unsigned y = 0; y < bh; ++y) for (unsigned x = 0; x < bw; ++x) {
        std::array<float,3> rgb;
        if(fullResolution) {
            bool red=f.front?(y%2 && x%2):(!(y%2) && x%2);
            bool blue=f.front?(!(y%2) && !(x%2)):(y%2 && !(x%2));
            float center=sample(x,y);
            float horizontal=(sample(int(x)-1,y)+sample(x+1,y))*.5f;
            float vertical=(sample(x,int(y)-1)+sample(x,y+1))*.5f;
            if(red || blue) {
                float diagonal=(sample(int(x)-1,int(y)-1)+sample(x+1,int(y)-1)+
                                sample(int(x)-1,y+1)+sample(x+1,y+1))*.25f;
                rgb=red?std::array<float,3>{center,(horizontal+vertical)*.5f,diagonal}
                       :std::array<float,3>{diagonal,(horizontal+vertical)*.5f,center};
            } else {
                bool redHorizontal=f.front?bool(y%2):!(y%2);
                rgb=redHorizontal?std::array<float,3>{horizontal,center,vertical}
                                 :std::array<float,3>{vertical,center,horizontal};
            }
        } else {
            float a=sample(x*2,y*2),b=sample(x*2+1,y*2);
            float c=sample(x*2,y*2+1),d=sample(x*2+1,y*2+1);
            rgb=f.front?std::array<float,3>{d,(b+c)*.5f,a}
                       :std::array<float,3>{b,(a+d)*.5f,c};
        }
        // Observed dark floor with the current sensor mode tables: 16 / 64 DN.
        // shortcut: fixed mode black level; replace with calibrated values for additional modes.
        const float black = f.front ? 16.f : 64.f;
        for (int k=0;k<3;++k) {
            rgb[k] = std::max(0.f,rgb[k]-black)*1023.f/(1023.f-black);
            sums[k] += rgb[k];
        }
        cells[size_t(y)*bw+x] = rgb;
    }
    float rg = std::clamp(float(sums[1] / std::max(sums[0],1.)), .5f, 3.f);
    float bg = std::clamp(float(sums[1] / std::max(sums[2],1.)), .5f, 3.f);
    redGain = .9f * redGain + .1f * rg;
    blueGain = .9f * blueGain + .1f * bg;
    mean=float((sums[0]+2*sums[1]+sums[2])/(4.*bw*bh*1023));
}
Image Isp::render(unsigned w,unsigned h) const {
    if(!w || !h || cells.empty())throw std::runtime_error("invalid ISP output");
    Image out{w,h,std::vector<uint8_t>(size_t(w)*h*3),mean};
    static const auto gamma=[] {
        std::array<uint8_t,4096> table{};
        for(unsigned i=0;i<table.size();++i) {
            float v=float(i)/4095;
            table[i]=uint8_t(std::lround(255*(v<=.0031308f?12.92f*v:
                                  1.055f*std::pow(v,1/2.4f)-.055f)));
        }
        return table;
    }();
    // Center crop to the requested aspect ratio, then bilinear resize.
    float cw = bw, ch = bh;
    if (cw*h > ch*w) cw = ch*w/h; else ch = cw*h/w;
    for (unsigned y=0;y<h;++y) for (unsigned x=0;x<w;++x) {
        float sx = std::clamp((bw-cw)/2+(x+.5f)*cw/w-.5f,0.f,float(bw-1));
        float sy = std::clamp((bh-ch)/2+(y+.5f)*ch/h-.5f,0.f,float(bh-1));
        unsigned ix=sx, iy=sy, jx=std::min(ix+1,bw-1), jy=std::min(iy+1,bh-1);
        float dx=sx-ix, dy=sy-iy;
        for (int k=0;k<3;++k) {
            float v = (cells[iy*bw+ix][k]*(1-dx)+cells[iy*bw+jx][k]*dx)*(1-dy)+
                      (cells[jy*bw+ix][k]*(1-dx)+cells[jy*bw+jx][k]*dx)*dy;
            v *= k==0 ? redGain : k==2 ? blueGain : 1.f;
            out.rgb[(size_t(y)*w+x)*3+k] = gamma[std::clamp(int(v*4),0,4095)];
        }
    }
    return out;
}
}
