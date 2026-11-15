// SPDX-License-Identifier: GPL-2.0-only
#include "spx-isp.h"
#include <cstdio>
#include <exception>
int main() {
    try {
        for(bool front : {true,false}) {
            spx::Capture cap(front,front?1296:2112,front?972:1568);
            spx::Isp isp;
            // Startup frames were observed to contain unsettled sensor state.
            cap.next(); cap.next();
            for(int n=0;n<40;++n) {
                auto f=cap.next();
                auto im=isp.process(f,640,480);
                cap.autoExposure(im.linearMean);
                if(n%4==0) printf("%s frame=%u mean=%.4f\n",front?"front":"rear",f.sequence,im.linearMean);
                if(n==39) {
                    FILE* file=fopen(front?"/data/local/tmp/spx-front.ppm":"/data/local/tmp/spx-rear.ppm","wb");
                    if(!file) return 2;
                    fprintf(file,"P6\n640 480\n255\n");
                    fwrite(im.rgb.data(),1,im.rgb.size(),file);
                    fclose(file);
                }
            }
        }
    } catch(const std::exception& e) {fprintf(stderr,"%s\n",e.what());return 1;}
}
