"""Exercise the actual generated AOSP helper against observable native-window calls."""
import argparse
from pathlib import Path
import subprocess
from test_sm8250_omx_hdr_sequence import block


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--helper', type=Path, required=True)
    parser.add_argument('--video-api', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    body = block(args.helper.read_text(), 'void setNativeWindowHdrMetadata(')
    hdr = block(args.video_api.read_text(), 'struct __attribute__ ((__packed__)) HDRStaticInfo') + ';'
    text = r"""
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cmath>
struct xy {float x,y;};
struct android_smpte2086_metadata {xy displayPrimaryRed,displayPrimaryGreen,displayPrimaryBlue,whitePoint;float maxLuminance,minLuminance;};
struct android_cta861_3_metadata {float maxContentLightLevel,maxFrameAverageLightLevel;};
struct ANativeWindow {bool mastering=false,light=false;int calls=0,fail=0,errors=0;android_smpte2086_metadata m{};android_cta861_3_metadata c{};};
int native_window_set_buffers_smpte2086_metadata(ANativeWindow *w,const android_smpte2086_metadata *v){++w->calls;if(w->fail)return w->fail;w->mastering=v!=nullptr;if(v)w->m=*v;return 0;}
int native_window_set_buffers_cta861_3_metadata(ANativeWindow *w,const android_cta861_3_metadata *v){++w->calls;if(w->fail)return w->fail;w->light=v!=nullptr;if(v)w->c=*v;return 0;}
#define ALOGW_IF(condition,...) do {if(condition)++nativeWindow->errors;}while(0)
@HDR@
@BODY@
int main(){
 ANativeWindow w{};HDRStaticInfo i{};
 i.sType1.mR={34000,16000};i.sType1.mMaxDisplayLuminance=1000;i.sType1.mMinDisplayLuminance=50;
 i.sType1.mMaxContentLightLevel=900;i.sType1.mMaxFrameAverageLightLevel=400;
 setNativeWindowHdrMetadata(&w,&i);assert(w.mastering && w.light && w.calls==2);
 assert(fabs(w.m.displayPrimaryRed.x-.68f)<.00001f && fabs(w.m.minLuminance-.005f)<.000001f);
 assert(w.m.maxLuminance==1000 && w.c.maxContentLightLevel==900);
 HDRStaticInfo empty{};setNativeWindowHdrMetadata(&w,&empty);assert(!w.mastering && !w.light && w.calls==4);
 setNativeWindowHdrMetadata(&w,&empty);assert(!w.mastering && !w.light && w.calls==6);
 setNativeWindowHdrMetadata(&w,&i);assert(w.mastering && w.light);
 i.sType1.mMaxContentLightLevel=0;setNativeWindowHdrMetadata(&w,&i);assert(w.mastering && !w.light);
 i.sType1.mMaxContentLightLevel=700;i.sType1.mMinDisplayLuminance=0;
 setNativeWindowHdrMetadata(&w,&i);assert(!w.mastering && w.light && w.c.maxContentLightLevel==700);
 w.fail=-22;setNativeWindowHdrMetadata(&w,&empty);assert(w.errors==2);
 puts("PASS actual SurfaceUtils helper: HDR, empty, repeated clear, new HDR, independent fields, units, errors");
}
""".replace('@HDR@', hdr).replace('@BODY@', body)
    args.output.mkdir(parents=True, exist_ok=True)
    cpp = args.output / 'surface.cpp'
    cpp.write_text(text)
    exe = args.output / 'surface-test'
    subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
