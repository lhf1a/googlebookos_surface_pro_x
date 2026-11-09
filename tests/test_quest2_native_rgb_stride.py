"""Exercise the exact RGB unit correction; unknown/invalid metadata stays untouched."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class NativeRGBStride(unittest.TestCase):
    def test_actual_c_format_units_bounds_and_unknowns(self):
        source = r"""
#include <assert.h>
#include "quest2-native-rgb-stride.h"
int main(void) {
    const int formats[]={1,2,3,4,5,22,43};
    const int sizes[]={4,4,3,2,4,8,4};
    for(unsigned i=0;i<7;i++) {
        int f=formats[i],b=sizes[i],pixels=192,h=65,bytes=pixels*b;
        assert(quest2_rgb_bpp(f)==b);
        assert(quest2_rgb_byte_stride(f,pixels,h,bytes*h,b,pixels)==bytes);
        assert(quest2_rgb_byte_stride(f,pixels,h,bytes*h,b,bytes)==bytes);
        assert(quest2_rgb_byte_stride(f,pixels,h,bytes*h,b,-1)==-1);
        assert(quest2_rgb_byte_stride(f,pixels,h,bytes*h-1,b,pixels)==pixels);
        assert(quest2_rgb_byte_stride(f,pixels,h,bytes*h,b+1,pixels)==pixels);
        assert(quest2_rgb_byte_stride(f,INT32_MAX,h,INT32_MAX,b,INT32_MAX)==INT32_MAX);
        assert(quest2_rgb_byte_stride(f,pixels,0,bytes*h,b,pixels)==pixels);
    }
    assert(quest2_rgb_bpp(0x36)==0);
    assert(quest2_rgb_byte_stride(0x36,192,64,99999,2,192)==192);
    assert(quest2_rgb_byte_stride(0x7fa30c04,512,512,393216,1,512)==512);
}
"""
        with tempfile.TemporaryDirectory() as td:
            path=Path(td);(path/"test.c").write_text(source)
            subprocess.run(["cc","-std=c11","-Wall","-Wextra","-Werror",
                "-fsanitize=address,undefined","-fno-pie","-no-pie",
                "-I",str(ROOT/"probe/graphics"),str(path/"test.c"),"-o",str(path/"test")],check=True)
            subprocess.run([str(path/"test")],check=True,timeout=10)

if __name__ == "__main__":
    unittest.main()
