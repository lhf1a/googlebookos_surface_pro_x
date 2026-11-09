"""Run the actual CSC selector and source transform; hardware boundary is not mocked as pixels."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / "out/qualcomm-full-stack-20261108/display-kernel-caps-fence-ubwc-v1/source/drivers/gpu/drm/msm/disp/dpu1"
spec = importlib.util.spec_from_file_location("patch", ROOT / "scripts/patch-sq2-dpu-sdr-color.py")
patch = importlib.util.module_from_spec(spec);spec.loader.exec_module(patch)

class DpuSdrColor(unittest.TestCase):
    @unittest.skipUnless((BASE / "dpu_plane.c").exists(), "requires matched Linux DPU source")
    def test_actual_source_transform_and_allocation_properties(self):
        text = patch.transform((BASE / "dpu_plane.c").read_text())
        self.assertIn("_dpu_plane_get_csc(pipe, format, pdpu->base.state)", text)
        self.assertIn("drm_plane_create_color_properties(plane", text)
        self.assertIn("!format->is_yuv", text)
        self.assertNotIn("csc_ptr = &dpu_csc10_YUV2RGB_601L", text)
        with self.assertRaises(ValueError):
            patch.transform(text)
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            (directory / "dpu_plane.c").write_text("unknown source")
            with self.assertRaises(ValueError):
                patch.patch(directory)
            self.assertEqual((directory / "dpu_plane.c").read_text(), "unknown source")
            self.assertFalse((directory / "dpu-sdr-color.h").exists())

    def test_actual_csc_selector_coefficients_and_rejections(self):
        source = r"""
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#include <math.h>
enum drm_color_encoding {DRM_COLOR_YCBCR_BT601,DRM_COLOR_YCBCR_BT709,DRM_COLOR_YCBCR_BT2020};
enum drm_color_range {DRM_COLOR_YCBCR_LIMITED_RANGE,DRM_COLOR_YCBCR_FULL_RANGE};
struct dpu_csc_cfg {uint32_t csc_mv[9],pre[3],post[3],pre_clamp[6],post_clamp[6];};
static const struct dpu_csc_cfg dpu_csc_YUV2RGB_601L={0},dpu_csc10_YUV2RGB_601L={0};
#include "dpu-sdr-color.h"
static double clamp(double v) {return fmin(255.,fmax(0.,v));}
int main(void) {
 assert(dpu_sdr_color_csc(DRM_COLOR_YCBCR_BT601,DRM_COLOR_YCBCR_LIMITED_RANGE,false)==&dpu_csc_YUV2RGB_601L);
 assert(dpu_sdr_color_csc(DRM_COLOR_YCBCR_BT601,DRM_COLOR_YCBCR_LIMITED_RANGE,true)==&dpu_csc10_YUV2RGB_601L);
 assert(!dpu_sdr_color_csc(DRM_COLOR_YCBCR_BT709,DRM_COLOR_YCBCR_FULL_RANGE,false));
 assert(!dpu_sdr_color_csc(DRM_COLOR_YCBCR_BT2020,DRM_COLOR_YCBCR_LIMITED_RANGE,true));
 assert(!dpu_sdr_color_csc((enum drm_color_encoding)99,DRM_COLOR_YCBCR_LIMITED_RANGE,false));
 const struct dpu_csc_cfg *eight=dpu_sdr_color_csc(DRM_COLOR_YCBCR_BT709,DRM_COLOR_YCBCR_LIMITED_RANGE,false);
 const struct dpu_csc_cfg *ten=dpu_sdr_color_csc(DRM_COLOR_YCBCR_BT709,DRM_COLOR_YCBCR_LIMITED_RANGE,true);
 assert(eight==&dpu_sdr709_8 && ten==&dpu_sdr709_10);
 assert(eight->pre[0]==0xfff0 && ten->pre[0]==0xffc0 && ten->pre[1]==0xfe00);
 for(unsigned i=0;i<9;i++)assert(eight->csc_mv[i]==ten->csc_mv[i]);
 for(int y=16;y<=235;y+=17)for(int cb=16;cb<=240;cb+=16)for(int cr=16;cr<=240;cr+=16) {
  double yy=(y-16)*255./219.,u=(cb-128)*255./224.,v=(cr-128)*255./224.;
  double expected[]={yy+2*(1-.2126)*v,yy-2*.0722*(1-.0722)/.7152*u-2*.2126*(1-.2126)/.7152*v,yy+2*(1-.0722)*u};
  for(int row=0;row<3;row++) {
   const uint32_t *m=eight->csc_mv+row*3;
   double got=((double)(int32_t)m[0]*(y-16)+(double)(int32_t)m[1]*(cb-128)+(double)(int32_t)m[2]*(cr-128))/65536.;
   assert(fabs(clamp(got)-clamp(expected[row]))<1.1);
  }
 }
 return 0;
}
"""
        with tempfile.TemporaryDirectory() as directory:
            c = Path(directory) / "test.c";binary = c.with_suffix("")
            c.write_text(source)
            subprocess.run(["gcc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                "-I", str(ROOT / "probe/graphics"), str(c), "-lm", "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
