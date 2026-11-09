"""Exercise the real YV12 boundary validator with donor layouts and invalid planes."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class YV12Layout(unittest.TestCase):
    def test_layouts_and_invalid_bounds(self):
        source=r"""
#include <cassert>
#include <climits>
#include "quest2-yv12-layout.h"
int main() {
    gralloc::PlaneLayoutInfo y{1,0,0,0,1,128,128,72,9216};
    gralloc::PlaneLayoutInfo cr{4,1,1,9216,1,128,64,36,2304};
    gralloc::PlaneLayoutInfo cb{2,1,1,11520,1,128,64,36,2304};
    auto valid=[&](unsigned size=16384) {
        return quest2_yv12_layout_valid(y,cr,cb,128,72,128,72,size);
    };
    assert(valid());assert(valid(13824));assert(!valid(13823));
    assert(!quest2_yv12_layout_valid(y,cb,cr,128,72,128,72,16384));
    assert(!quest2_yv12_layout_valid(y,cr,cb,129,72,128,72,16384));
    assert(!quest2_yv12_layout_valid(y,cr,cb,128,73,128,72,16384));
    assert(!quest2_yv12_layout_valid(y,cr,cb,0,72,128,72,16384));
    assert(!quest2_yv12_layout_valid(y,cr,cb,128,72,0,72,16384));
    assert(!quest2_yv12_layout_valid(y,cr,cb,128,72,128,0,16384));
    assert(!quest2_yv12_layout_valid(y,cr,cb,128,72,129,72,16384));
    assert(!quest2_yv12_layout_valid(y,cr,cb,128,72,128,73,16384));
    assert(!quest2_yv12_layout_valid(y,cr,cb,128,72,UINT_MAX,UINT_MAX,UINT_MAX));
    assert(!quest2_yv12_layout_valid(y,cr,cb,128,72,INT_MAX-15,INT_MAX-1,UINT_MAX));
    for(unsigned i=0;i<3;i++) {
        auto &p=i==2?cb:i?cr:y;const auto saved=p;
        p.component^=1;assert(!valid());p=saved;
        ++p.h_subsampling;assert(!valid());p=saved;
        ++p.v_subsampling;assert(!valid());p=saved;
        ++p.offset;assert(!valid());p=saved;
        p.offset=UINT_MAX;assert(!valid());p=saved;
        ++p.step;assert(!valid());p=saved;
        ++p.stride;assert(!valid());p=saved;
        p.stride_bytes=-1;assert(!valid());p=saved;
        --p.scanlines;assert(!valid());p=saved;
        --p.size;assert(!valid());p=saved;
    }
    y={1,0,0,0,1,272,272,130,35360};
    cr={4,1,1,35360,1,272,144,65,9360};
    cb={2,1,1,44720,1,272,144,65,9360};
    assert(quest2_yv12_layout_valid(y,cr,cb,258,130,272,130,57344));
    cr.stride_bytes=136;
    assert(!quest2_yv12_layout_valid(y,cr,cb,258,130,272,130,57344));
}
"""
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);(p/"test.cpp").write_text(source)
            subprocess.run(["c++","-std=c++17","-Wall","-Wextra","-Werror",
                "-fsanitize=address,undefined","-fno-pie","-no-pie",
                "-I",str(ROOT/"probe/graphics"),str(p/"test.cpp"),"-o",str(p/"test")],check=True)
            subprocess.run([str(p/"test")],check=True,timeout=10)

if __name__=="__main__":unittest.main()
