#!/usr/bin/env python3
"""Expose real initialized-HFI MIN_OUTPUT; pre-session queries return EAGAIN."""
from pathlib import Path
import argparse, shutil

def apply(directory, root):
 p=directory/'vdec_ctrls.c'
 s=p.read_text()
 def replace(a,b):
  nonlocal s
  if s.count(a)!=1: raise ValueError('Unknown MIN_OUTPUT anchor: '+repr(a))
  s=s.replace(a,b,1)
 replace('#include "vdec.h"','#include "vdec.h"\n#include "venus-sq2-min-output.h"')
 replace('\tcase V4L2_CID_MIN_BUFFERS_FOR_CAPTURE:\n',
  '\tcase V4L2_CID_MIN_BUFFERS_FOR_OUTPUT:\n\t\treturn venus_sq2_min_output_ctrl(inst, ctrl);\n'
  '\tcase V4L2_CID_MIN_BUFFERS_FOR_CAPTURE:\n')
 anchor='\tv4l2_ctrl_new_std(&inst->ctrl_handler, &vdec_ctrl_ops,\n\t\t\t  V4L2_CID_MPEG_VIDEO_DEC_DISPLAY_DELAY,'
 replace(anchor,
  '\tctrl = v4l2_ctrl_new_std(&inst->ctrl_handler, &vdec_ctrl_ops,\n'
  '\t\tV4L2_CID_MIN_BUFFERS_FOR_OUTPUT, 0, INT_MAX, 1, 0);\n'
  '\tif (ctrl)\n\t\tctrl->flags |= V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_READ_ONLY;\n\n'+anchor)
 # Never mask a firmware query error as the control's previous/default value.
 replace('\tcase V4L2_CID_MIN_BUFFERS_FOR_CAPTURE:\n\t\tret = venus_helper_get_bufreq(inst, HFI_BUFFER_OUTPUT, &bufreq);\n\t\tif (!ret)\n\t\t\tctrl->val = HFI_BUFREQ_COUNT_MIN(&bufreq, ver);\n\t\tbreak;',
  '\tcase V4L2_CID_MIN_BUFFERS_FOR_CAPTURE:\n\t\tret = venus_helper_get_bufreq(inst, HFI_BUFFER_OUTPUT, &bufreq);\n\t\tif (ret)\n\t\t\treturn ret;\n\t\tctrl->val = HFI_BUFREQ_COUNT_MIN(&bufreq, ver);\n\t\tbreak;')
 p.write_text(s)
 shutil.copyfile(root/'probe/video/venus-sq2-min-output.h',directory/'venus-sq2-min-output.h')
 p=directory/'vdec.c';s=p.read_text()
 replace('static int vdec_close(struct file *file)\n{\n\tstruct venus_inst *inst = to_inst(file);',
  'static int vdec_close(struct file *file)\n{\n\tstruct venus_inst *inst = to_inst(file);\n\tint ret, end = 0;')
 replace('\tsq2_callback_close_begin(inst);\n\tvdec_pm_get(inst);',
  '\tsq2_callback_close_begin(inst);\n\tret = vdec_pm_get(inst);')
 replace('\tv4l2_m2m_ctx_release(inst->m2m_ctx);\n\tv4l2_m2m_release(inst->m2m_dev);\n\tvdec_ctrl_deinit(inst);',
  '\tv4l2_m2m_ctx_release(inst->m2m_ctx);\n\tv4l2_m2m_release(inst->m2m_dev);\n'
  '\t/* REQBUFS may initialize HFI without ever reaching STREAMON. VB2 then\n'
  '\t * skips stop_streaming; confirm END before dropping the live session.\n'
  '\t * Failed END keeps the existing scope/DMA retirement quarantine. */\n'
  '\tif (IS_SQ2_HFI(inst->core)) {\n'
  '\t\tend = hfi_session_deinit(inst);\n'
  '\t\tif (end)\n'
  '\t\t\tdev_err(inst->core->dev, "SQ2 close SESSION_END=%d; retirement retained\\n", end);\n'
  '\t}\n\tvdec_ctrl_deinit(inst);')
 replace('\tvdec_pm_put(inst, false);\n\n\tkfree(inst);\n\treturn 0;\n}',
  '\tif (ret >= 0)\n\t\tvdec_pm_put(inst, false);\n\n\tkfree(inst);\n\treturn ret < 0 ? ret : end;\n}')
 p.write_text(s)

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path);p.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[1]);a=p.parse_args();apply(a.directory,a.root)
