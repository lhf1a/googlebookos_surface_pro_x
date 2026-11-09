"""Check the real RGB payload validator against captured donor layouts and bad boundaries."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class RGBLayout(unittest.TestCase):
    def test_captured_layouts_and_invalid_bounds(self):
        source=r"""
#include <cassert>
#include <climits>
#include "quest2-rgb-layout.h"
int main() {
    gralloc::PlaneLayoutInfo x{0x101c00,0,0,4096,4,192,768,80,65536};
    assert(quest2_rgb_layout_valid(x,1,192,80,65536,true,4096));
    assert(!quest2_rgb_layout_valid(x,1,192,80,65536,false,0));
    assert(!quest2_rgb_layout_valid(x,5,192,80,65536,true,4096));
    assert(!quest2_rgb_layout_valid(x,1,192,80,65535,true,4096));
    assert(!quest2_rgb_layout_valid(x,1,192,80,65536,true,0));
    assert(!quest2_rgb_layout_valid(x,1,192,80,65536,true,65536));
    assert(!quest2_rgb_layout_valid(x,1,192,80,65536,true,UINT_MAX));
    x.offset=8192;
    assert(!quest2_rgb_layout_valid(x,1,192,80,65536,true,8192));
    x.offset=1;
    assert(!quest2_rgb_layout_valid(x,1,192,80,65536,true,1));
    x.offset=0;x.size=61440;
    assert(quest2_rgb_layout_valid(x,1,192,80,65536,false,0));
    --x.size;assert(!quest2_rgb_layout_valid(x,1,192,80,65536,false,0));
    x={0x1c00,0,0,94208,4,2880,11520,1920,22212608};
    assert(quest2_rgb_layout_valid(x,2,2880,1920,22212608,true,94208));
    x.component|=1u<<20;
    assert(quest2_rgb_layout_valid(x,43,2880,1920,22212608,true,94208));
    x={0x101c00,0,0,184320,8,2880,23040,1920,44421120};
    assert(quest2_rgb_layout_valid(x,22,2880,1920,44421120,true,184320));
    x.stride_bytes=-1;assert(!quest2_rgb_layout_valid(x,22,2880,1920,44421120,true,184320));
    x={0x101c00,0,0,4096,4,INT_MAX,INT_MAX,INT_MAX,UINT_MAX};
    assert(!quest2_rgb_layout_valid(x,1,INT_MAX,INT_MAX,UINT_MAX,true,4096));
    assert(!quest2_rgb_layout_valid(x,1,UINT_MAX,UINT_MAX,UINT_MAX,true,4096));
    assert(!quest2_rgb_layout_valid(x,0x36,192,80,65536,true,4096));
}
"""
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);(p/"test.cpp").write_text(source)
            subprocess.run(["c++","-std=c++17","-Wall","-Wextra","-Werror",
                "-fsanitize=address,undefined","-fno-pie","-no-pie",
                "-I",str(ROOT/"probe/graphics"),str(p/"test.cpp"),"-o",str(p/"test")],check=True)
            subprocess.run([str(p/"test")],check=True,timeout=10)

if __name__=="__main__":unittest.main()
