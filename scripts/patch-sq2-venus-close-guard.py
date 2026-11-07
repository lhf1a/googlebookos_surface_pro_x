#!/usr/bin/env python3
"""Guard SQ2 close PM ownership and quarantine unresolved HFI session IDs."""
from pathlib import Path
import argparse, shutil

def apply(directory, root):
 p=directory/'vdec.c'; s=p.read_text()
 def replace(a,b):
  nonlocal s
  if s.count(a)!=1: raise ValueError('Unknown close guard anchor: '+repr(a))
  s=s.replace(a,b,1)
 replace('#include "venus-sq2-callback.h"','#include "venus-sq2-callback.h"\n#include "venus-sq2-close-guard.h"')
 replace('static void vdec_stop_streaming(struct vb2_queue *q)\n{\n\tstruct venus_inst *inst = vb2_get_drv_priv(q);\n\tint ret = -EINVAL;\n\n\tvdec_pm_get_put(inst);',
 'static void vdec_stop_streaming(struct vb2_queue *q)\n{\n\tstruct venus_inst *inst = vb2_get_drv_priv(q);\n\tint ret;\n\n\tret = IS_SQ2_HFI(inst->core) && test_bit(0, &inst->core->sys_error) ?\n\t\t-EIO : vdec_pm_get_put(inst);\n\tif (ret < 0) {\n\t\tvenus_sq2_quarantine_unresolved(inst, ret, "stop-streaming-power");\n\t\tsq2_callback_close_begin(inst);\n\t\tmutex_lock(&inst->lock);\n\t\tinst->session_error = true;\n\t\tvenus_helper_buffers_done(inst, q->type, VB2_BUF_STATE_ERROR);\n\t\tmutex_unlock(&inst->lock);\n\t\treturn;\n\t}')
 replace('static void vdec_session_release(struct venus_inst *inst)\n{\n\tstruct venus_core *core = inst->core;\n\tint ret, abort = 0;\n\n\tvdec_pm_get(inst);\n\n\tmutex_lock(&inst->lock);\n\tinst->codec_state = VENUS_DEC_STATE_DEINIT;',
 'static void vdec_session_release(struct venus_inst *inst)\n{\n\tstruct venus_core *core = inst->core;\n\tint ret, power, abort = 0;\n\n\tpower = IS_SQ2_HFI(core) && test_bit(0, &core->sys_error) ?\n\t\t-EIO : vdec_pm_get(inst);\n\tif (power < 0)\n\t\tvenus_sq2_quarantine_unresolved(inst, power, "release-power");\n\n\tmutex_lock(&inst->lock);\n\tinst->codec_state = VENUS_DEC_STATE_DEINIT;\n\tif (power < 0 || (IS_SQ2_HFI(core) && test_bit(0, &core->sys_error))) {\n\t\tinst->session_error = true;\n\t\tgoto retire_only;\n\t}')
 replace('\tret = hfi_session_deinit(inst);\n\tif (IS_SQ2_HFI(core)) abort |= (ret && ret != -EINVAL);\n\telse abort = (ret && ret != -EINVAL) ? 1 : 0;',
 '\tret = hfi_session_deinit(inst);\n\tif (IS_SQ2_HFI(core)) {\n\t\tif (venus_sq2_quarantine_unresolved(inst, ret ? ret : -EIO,\n\t\t\t\t\t\t "release-end"))\n\t\t\tgoto retire_only;\n\t} else abort = (ret && ret != -EINVAL) ? 1 : 0;')
 replace('\tif (abort)\n\t\thfi_session_abort(inst);\n\n\tvenus_helper_free_dpb_bufs(inst);',
 '\tif (abort && !IS_SQ2_HFI(core))\n\t\thfi_session_abort(inst);\n\n\tvenus_helper_free_dpb_bufs(inst);')
 replace('\tvenus_pm_release_core(inst);\n\tvdec_pm_put(inst, false);\n}\n\nstatic int vdec_buf_init',
 '\tvenus_pm_release_core(inst);\n\tvdec_pm_put(inst, false);\n\treturn;\n\nretire_only:\n\t/* No HFI or provider operation after failed power acquisition. Retired\n\t * backing owns its scope/device; it does not retain this instance. */\n\tmutex_unlock(&inst->lock);\n\t/* REQBUFS(0), unlike close(), has no earlier callback barrier. DPB\n\t * completion walks the list without inst->lock. Drain before retiring;\n\t * never wait with inst->lock held (event callbacks acquire it). */\n\tsq2_callback_close_begin(inst);\n\tmutex_lock(&inst->lock);\n\tvenus_helper_sq2_retire_buffers(inst);\n\tINIT_LIST_HEAD(&inst->registeredbufs);\n\tmutex_unlock(&inst->lock);\n\tif (power >= 0)\n\t\tvdec_pm_put(inst, false);\n}\n\nstatic int vdec_buf_init')
 replace('\tsq2_callback_close_begin(inst);\n\tret = vdec_pm_get(inst);',
 '\tsq2_callback_close_begin(inst);\n\tret = IS_SQ2_HFI(inst->core) && test_bit(0, &inst->core->sys_error) ?\n\t\t-EIO : vdec_pm_get(inst);\n\tif (ret < 0)\n\t\tvenus_sq2_quarantine_unresolved(inst, ret, "close-power");')
 replace('\tif (IS_SQ2_HFI(inst->core)) {\n\t\tend = hfi_session_deinit(inst);\n\t\tif (end)\n\t\t\tdev_err(inst->core->dev, "SQ2 close SESSION_END=%d; retirement retained\\n", end);\n\t}',
 '\tif (IS_SQ2_HFI(inst->core)) {\n\t\tif (ret >= 0 && !test_bit(0, &inst->core->sys_error))\n\t\t\tend = hfi_session_deinit(inst);\n\t\telse\n\t\t\tend = ret < 0 ? ret : -EIO;\n\t\tif (venus_sq2_quarantine_unresolved(inst, end ? end : -EIO,\n\t\t\t\t\t\t "close-end") && !end)\n\t\t\tend = -EIO;\n\t}')
 p.write_text(s)
 shutil.copyfile(root/'probe/video/venus-sq2-close-guard.h',directory/'venus-sq2-close-guard.h')

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('directory',type=Path);p.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[1]);a=p.parse_args();apply(a.directory,a.root)
