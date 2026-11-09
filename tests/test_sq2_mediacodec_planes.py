"""Run the actual ImageReader plane copier, including 16-bit last-sample bounds."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class ImagePlanes(unittest.TestCase):
    def test_byte_and_word_planes(self):
        source=(ROOT/"probe/video/sq2-mediacodec-frames.c").read_text()
        helper=source[source.index("static int copy_image_plane("):source.index("static int save_image(")]
        code="#include <stdint.h>\n#include <stddef.h>\n#include <string.h>\n#include <assert.h>\n"+helper+r"""
int main(void) {
 uint8_t src[]={0x10,0x11,0xaa,0xaa,0x20,0x21,0xaa,0xaa,
                0x30,0x31,0xaa,0xaa,0x40,0x41};
 uint8_t out[8]={0};
 const uint8_t expected[]={0x10,0x11,0x20,0x21,0x30,0x31,0x40,0x41};
 assert(copy_image_plane(out,8,src,14,8,4,2,2,2));
 assert(!memcmp(out,expected,8));
 assert(!copy_image_plane(out,8,src,13,8,4,2,2,2));
 assert(!copy_image_plane(out,7,src,14,8,4,2,2,2));
 assert(!copy_image_plane(out,8,src,14,5,4,2,2,2));
 assert(!copy_image_plane(out,8,src,14,8,1,2,2,2));
 assert(!copy_image_plane(out,8,src,-1,8,4,2,2,2));
 assert(!copy_image_plane(out,8,src,14,-1,4,2,2,2));
 assert(!copy_image_plane(out,8,src,14,8,4,0,2,2));
 assert(!copy_image_plane(out,8,src,14,8,4,2,0,2));
 assert(!copy_image_plane(out,8,src,14,8,4,2,2,3));
 assert(!copy_image_plane(out,8,src,14,8,4,UINT32_MAX,UINT32_MAX,2));
 assert(copy_image_plane(out,4,src,13,8,4,2,2,1));
 const uint8_t expected8[]={0x10,0x20,0x30,0x40};
 assert(!memcmp(out,expected8,4));
 assert(copy_image_plane(out,4,src,4,2,1,2,2,1));
 assert(!memcmp(out,src,4));
 return 0;
}
"""
        with tempfile.TemporaryDirectory() as directory:
            d=Path(directory);(d/"check.c").write_text(code)
            p=subprocess.run(["gcc","-std=gnu11","-Wall","-Wextra","-Werror","-fsanitize=address,undefined",
                "-fno-pie","-no-pie",str(d/"check.c"),"-o",str(d/"check")],capture_output=True,text=True)
            self.assertEqual(p.returncode,0,p.stderr)
            p=subprocess.run([str(d/"check")],capture_output=True,text=True)
            self.assertEqual(p.returncode,0,p.stdout+p.stderr)
