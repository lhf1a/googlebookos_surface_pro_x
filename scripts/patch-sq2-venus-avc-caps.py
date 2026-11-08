#!/usr/bin/env python3
"""Wire SQ2 AVC control menus and read-only export to validated HFI capabilities."""
from pathlib import Path
import argparse
import shutil
ROOT=Path(__file__).resolve().parents[1]
def apply(d):
 def replace(name,old,new):
  p=d/name;s=p.read_text()
  if s.count(old)!=1:raise ValueError('Unexpected anchor in '+name)
  p.write_text(s.replace(old,new))
 replace('venus-sq2-system-pm.h','static const char *sq2_pm_stage',
  '#include "venus-sq2-avc-export.h"\nstatic const char *sq2_pm_stage')
 shutil.copyfile(ROOT/'probe/video/venus-sq2-avc-export.h',d/'venus-sq2-avc-export.h')
 replace('helpers.c','{ HFI_H264_LEVEL_52, V4L2_MPEG_VIDEO_H264_LEVEL_5_1 }',
  '{ HFI_H264_LEVEL_52, V4L2_MPEG_VIDEO_H264_LEVEL_5_2 }')
 # Qualcomm downstream and OMX define AVC 6/6.1/6.2 as bits 17..19.
 replace('hfi_helper.h', '#define HFI_H264_LEVEL_52\t\t\t0x00010000',
  '#define HFI_H264_LEVEL_52\t\t\t0x00010000\n'
  '#define HFI_H264_LEVEL_6\t\t\t0x00020000\n'
  '#define HFI_H264_LEVEL_61\t\t\t0x00040000\n'
  '#define HFI_H264_LEVEL_62\t\t\t0x00080000')
 replace('helpers.c', '{ HFI_H264_LEVEL_52, V4L2_MPEG_VIDEO_H264_LEVEL_5_2 },',
  '{ HFI_H264_LEVEL_52, V4L2_MPEG_VIDEO_H264_LEVEL_5_2 },\n'
  '\t{ HFI_H264_LEVEL_6, V4L2_MPEG_VIDEO_H264_LEVEL_6_0 },\n'
  '\t{ HFI_H264_LEVEL_61, V4L2_MPEG_VIDEO_H264_LEVEL_6_1 },\n'
  '\t{ HFI_H264_LEVEL_62, V4L2_MPEG_VIDEO_H264_LEVEL_6_2 },')
 helper = r'''int venus_helper_sq2_avc_controls(struct venus_core *core, u64 *mask, u32 *level)
{
 const struct hfi_plat_caps *c = venus_caps_by_codec(core, HFI_VIDEO_CODEC_H264, VIDC_SESSION_TYPE_DEC);
 unsigned int i, j;
 u32 p, l;
 *mask = 0; *level = 0;
 if (!c || !c->num_pl) return -ENODATA;
 for (i=0; i<c->num_pl; i++) {
  p = U32_MAX; l = U32_MAX;
  for (j=0; j<ARRAY_SIZE(h264_profiles); j++)
   if (h264_profiles[j].hfi_id == c->pl[i].profile) p = h264_profiles[j].v4l2_id;
  for (j=0; j<ARRAY_SIZE(h264_levels); j++)
   if (h264_levels[j].hfi_id == c->pl[i].level) l = h264_levels[j].v4l2_id;
  if (p >= 64 || l == U32_MAX) return -EINVAL;
  *mask |= BIT_ULL(p); *level = max(*level, l);
 }
 return 0;
}
EXPORT_SYMBOL_GPL(venus_helper_sq2_avc_controls);

'''
 replace('helpers.c','int venus_helper_get_profile_level(',helper+'int venus_helper_get_profile_level(')
 p=d/'helpers.h';p.write_text(p.read_text()+'\nint venus_helper_sq2_avc_controls(struct venus_core *core, u64 *mask, u32 *level);\n')
 p=d/'vdec_ctrls.c';s=p.read_text();a=s.index('\tctrl = v4l2_ctrl_new_std_menu',s.index('V4L2_MPEG_VIDEO_MPEG4_LEVEL_0);'));b=s.index('\tctrl = v4l2_ctrl_new_std_menu',s.index('V4L2_MPEG_VIDEO_H264_LEVEL_1_0);',a))
 old=s[a:b]
 new=r''' if (IS_SQ2_HFI(inst->core)) {
  u64 mask; u32 level;
  ret = venus_helper_sq2_avc_controls(inst->core, &mask, &level);
  if (ret) { v4l2_ctrl_handler_free(&inst->ctrl_handler); return ret; }
  ctrl = v4l2_ctrl_new_std_menu(&inst->ctrl_handler, &vdec_ctrl_ops,
    V4L2_CID_MPEG_VIDEO_H264_PROFILE, fls64(mask)-1, ~mask, __ffs64(mask));
  if (ctrl) ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;
  ctrl = v4l2_ctrl_new_std_menu(&inst->ctrl_handler, &vdec_ctrl_ops,
    V4L2_CID_MPEG_VIDEO_H264_LEVEL, level, 0, V4L2_MPEG_VIDEO_H264_LEVEL_1_0);
  if (ctrl) ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;
 } else {
'''+old+' }\n\n'
 p.write_text(s[:a]+new+s[b:])
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path);apply(p.parse_args().directory)
