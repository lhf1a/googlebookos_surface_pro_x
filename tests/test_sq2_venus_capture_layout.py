"""Compile the actual layout arithmetic and negotiated-format helpers."""
import ast
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("capture_patch", ROOT / "scripts/patch-sq2-venus-capture-layout.py")
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)


class CaptureLayoutTest(unittest.TestCase):
    def test_real_c_geometry(self):
        source = r"""
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <linux/videodev2.h>
typedef uint32_t u32;
#define max(a,b) ((a)>(b)?(a):(b))
#define ALIGN(x,a) (((x)+(a)-1)&~((a)-1))
#include "venus-sq2-capture-layout.h"
int main(void) {
 u32 size=0;
 assert(!sq2_capture_layout(V4L2_PIX_FMT_NV12,320,256,512,512,159744,&size) && size==393216);
 assert(!sq2_capture_layout(V4L2_PIX_FMT_P010,320,256,768,256,294912,&size) && size==294912);
 assert(!sq2_capture_layout(V4L2_PIX_FMT_NV12,320,256,384,256,159744,&size) && size==159744);
 assert(!sq2_capture_layout(V4L2_PIX_FMT_P010,8192,8192,16384,8192,0,&size) && size==201326592);
 assert(sq2_capture_layout(V4L2_PIX_FMT_NV12,0,256,512,512,0,&size)==-EINVAL);
 assert(sq2_capture_layout(V4L2_PIX_FMT_NV12,320,256,UINT32_MAX,512,0,&size)==-EINVAL);
 assert(sq2_capture_layout(V4L2_PIX_FMT_NV12,320,256,512,UINT32_MAX,0,&size)==-EINVAL);
 assert(sq2_capture_layout(V4L2_PIX_FMT_NV12,320,256,383,256,0,&size)==-EINVAL);
 assert(sq2_capture_layout(V4L2_PIX_FMT_NV12,320,256,512,255,0,&size)==-EINVAL);
 assert(sq2_capture_layout(V4L2_PIX_FMT_P010,320,256,512,256,0,&size)==-EINVAL);
 assert(sq2_capture_layout(V4L2_PIX_FMT_YUV420,320,256,512,256,0,&size)==-EINVAL);
 return 0;
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            (d / "test.c").write_text(source)
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                            "-fno-pie", "-no-pie", "-I", str(ROOT / "probe/video"),
                            str(d / "test.c"), "-o", str(d / "test")], check=True)
            subprocess.run([str(d / "test")], check=True)

    def test_negotiated_format_helpers(self):
        tree = ast.parse((ROOT / "scripts/patch-sq2-venus-capture-layout.py").read_text())
        helper = next(n.value.value for n in ast.walk(tree) if isinstance(n, ast.Assign)
                      and any(isinstance(t, ast.Name) and t.id == "helper" for t in n.targets))
        preamble = r"""
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <linux/videodev2.h>
typedef uint32_t u32;
#define max(a,b) ((a)>(b)?(a):(b))
#define ALIGN(x,a) (((x)+(a)-1)&~((a)-1))
#define IS_SQ2_HFI(core) (core)
#define VENUS_DEC_STATE_CAPTURE_SETUP 2
struct format {u32 pixfmt;};
struct venus_inst {int core,reconfig,codec_state;u32 width,height,sq2_capture_stride,sq2_capture_scan,sq2_capture_size;struct format *fmt_cap;};
static u32 venus_helper_get_framesz(u32 fmt,u32 w,u32 h){assert(fmt==V4L2_PIX_FMT_NV12&&w==320&&h==256);return 159744;}
#include "venus-sq2-capture-layout.h"
"""
        main = r"""
int main(void) {
 struct format fmt={V4L2_PIX_FMT_NV12};
 struct venus_inst i={.core=1,.reconfig=1,.codec_state=2,.width=320,.height=256,.fmt_cap=&fmt};
 struct v4l2_format f={.type=V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE};
 f.fmt.pix_mp=(struct v4l2_pix_format_mplane){.width=320,.height=512,.pixelformat=V4L2_PIX_FMT_NV12,.field=V4L2_FIELD_NONE};
 f.fmt.pix_mp.plane_fmt[0].bytesperline=384;
 struct v4l2_pix_format_mplane req=f.fmt.pix_mp;
 req.plane_fmt[0].bytesperline=512;
 assert(sq2_capture_try(&i,&f,&req)==1);
 assert(f.fmt.pix_mp.height==512&&f.fmt.pix_mp.plane_fmt[0].bytesperline==512&&f.fmt.pix_mp.plane_fmt[0].sizeimage==393216);
 assert(i.width==320&&i.height==256);
 i.reconfig=0;assert(sq2_capture_try(&i,&f,&req)==-EINVAL);i.reconfig=1;
 i.codec_state=3;assert(sq2_capture_try(&i,&f,&req)==-EINVAL);i.codec_state=2;
 req.width=321;assert(sq2_capture_try(&i,&f,&req)==-EINVAL);req.width=320;
 req.pixelformat=V4L2_PIX_FMT_P010;assert(sq2_capture_try(&i,&f,&req)==-EINVAL);req.pixelformat=V4L2_PIX_FMT_NV12;
 req.height=513;assert(sq2_capture_try(&i,&f,&req)==-EINVAL);req.height=512;
 req.plane_fmt[0].bytesperline=UINT32_MAX;assert(sq2_capture_try(&i,&f,&req)==-EINVAL);
 req.height=256;req.plane_fmt[0].bytesperline=384;assert(sq2_capture_try(&i,&f,&req)==0);
 i.sq2_capture_stride=512;i.sq2_capture_scan=512;i.sq2_capture_size=393216;
 sq2_capture_report(&i,&f);assert(f.fmt.pix_mp.height==512&&f.fmt.pix_mp.plane_fmt[0].sizeimage==393216);
 f.type=V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;f.fmt.pix_mp.height=240;sq2_capture_report(&i,&f);assert(f.fmt.pix_mp.height==240);
 return 0;
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            (d / "test.c").write_text(preamble + helper + main)
            subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                            "-fno-pie", "-no-pie", "-I", str(ROOT / "probe/video"),
                            str(d / "test.c"), "-o", str(d / "test")], check=True)
            subprocess.run([str(d / "test")], check=True)

    def test_unknown_input_preserved(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            files = {"core.h": "	u32 sq2_extra_mask, sq2_extra_size;", "vdec.c": "unknown", "hfi_cmds.c": "unknown"}
            for name, body in files.items():
                (d / name).write_text(body)
            with self.assertRaises(ValueError):
                patch.apply(d)
            self.assertEqual({name: (d / name).read_text() for name in files}, files)
            self.assertFalse((d / "venus-sq2-capture-layout.h").exists())


if __name__ == "__main__":
    unittest.main()
