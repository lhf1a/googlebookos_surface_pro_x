#!/usr/bin/env python3
"""Add real HFI sequence metadata and acknowledged OMX flush to paired Venus."""
from pathlib import Path
import argparse, shutil


def apply_codec_config(directory, root):
    helper = directory/'helpers.c'
    text = helper.read_text()
    anchor = '\t\tif (inst->session_type == VIDC_SESSION_TYPE_DEC)\n\t\t\tput_ts_metadata(inst, vbuf);'
    replacement = ('\t\tif (inst->session_type == VIDC_SESSION_TYPE_DEC) {\n'
                   '\t\t\tif (IS_SQ2_HFI(inst->core) && sq2_omx_input_flags(vbuf->flags))\n'
                   '\t\t\t\tfdata.flags |= sq2_omx_input_flags(vbuf->flags);\n'
                   '\t\t\telse\n\t\t\t\tput_ts_metadata(inst, vbuf);\n\t\t}')
    if text.count(anchor) != 1 or text.count('#include "helpers.h"') != 1:
        raise ValueError('Unknown Venus input flag anchor')
    text = text.replace(anchor, replacement).replace('#include "helpers.h"', '#include "helpers.h"\n#include "venus-sq2-omx-flags.h"')
    dec = directory/'vdec.c'
    decoder = dec.read_text()
    anchor = '#include "venus-sq2-omx.h"'
    if decoder.count(anchor) != 1 or 'sq2_omx_config_flag' in decoder:
        raise ValueError('Unknown or already modified decoder capability anchor')
    decoder = decoder.replace(anchor, anchor + '\n#include "venus-sq2-omx-flags.h"\n'
        'static unsigned int sq2_omx_config_flag = SQ2_V4L2_BUF_FLAG_CODECCONFIG;\n'
        'module_param(sq2_omx_config_flag, uint, 0444);\n'
        'MODULE_PARM_DESC(sq2_omx_config_flag, "Native V4L2 flag translated to HFI CODECCONFIG");')
    helper.write_text(text)
    dec.write_text(decoder)
    shutil.copyfile(root/'probe/video/venus-sq2-omx-flags.h', directory/'venus-sq2-omx-flags.h')


def apply_late_events(directory):
    path = directory/'hfi_msgs.c'
    text = path.read_text()
    original = ('\tstruct hfi_msg_event_notify_pkt *pkt = packet;\n\n'
                '\tif (!packet)\n\t\treturn;')
    guarded = ('\tstruct hfi_msg_event_notify_pkt *pkt = packet;\n\n'
               '\t/* Closing sessions reject callbacks; only SYS_ERROR is sessionless. */\n'
               '\tif (!packet || (!inst && pkt->event_id != HFI_EVENT_SYS_ERROR))\n\t\treturn;')
    detailed = ('\tstruct hfi_msg_event_notify_pkt *pkt = packet;\n\n'
                '\tif (!packet)\n\t\treturn;\n'
                '\t/* Closing sessions reject callbacks; only SYS_ERROR is sessionless. */\n'
                '\tif (!inst && pkt->event_id != HFI_EVENT_SYS_ERROR) {\n'
                '\t\tif (pkt->shdr.hdr.size >= offsetof(struct hfi_msg_event_notify_pkt, ext_event_data))\n'
                '\t\t\tdev_warn_ratelimited(core->dev,\n'
                '\t\t\t\t"late HFI event session:%x event:%x data1:%x data2:%x\\n",\n'
                '\t\t\t\tpkt->shdr.session_id, pkt->event_id,\n'
                '\t\t\t\tpkt->event_data1, pkt->event_data2);\n'
                '\t\treturn;\n\t}')
    before = guarded if text.count(guarded) == 1 else original
    changes = [(before, detailed)]
    for old, new in [
        ('dev_warn(dev, "no valid instance(pkt session_id:%x, pkt:%x)\\n",',
         'dev_warn_ratelimited(dev, "no valid instance(pkt session_id:%x, pkt:%x)\\n",'),
        ('dev_err(dev, "got invalid session id:%x\\n",',
         'dev_err_ratelimited(dev, "got invalid session id:%x\\n",'),
    ]:
        changes.append((old if text.count(old) == 1 else new, new))
    for before, after in changes:
        if text.count(before) != 1:
            raise ValueError('Unknown late HFI event anchor: ' + repr(before))
        text = text.replace(before, after, 1)
    path.write_text(text)


def apply_flush_lock(directory, root):
    path = directory/'core.h'
    text = path.read_text()
    anchor = '\tstruct mutex ctx_q_lock;'
    fields = ('\n\tu32 sq2_omx_flush_flags;'
              '\n\tbool sq2_omx_flush_output_pending;')
    if text.count(anchor) != 1 or 'sq2_omx_flush_flags' in text:
        raise ValueError('Unknown or already modified OMX queue lock layout')
    decoder = directory/'vdec.c'
    source = decoder.read_text()
    anchor_flush = '\t\tret = hfi_session_flush(inst, HFI_FLUSH_OUTPUT, false);'
    if source.count(anchor_flush) != 1:
        raise ValueError('Unknown DRC output flush anchor')
    source = source.replace(anchor_flush,
        '\t\tret = IS_SQ2_HFI(inst->core) ? sq2_omx_flush_output(inst) :\n'
        '\t\t\thfi_session_flush(inst, HFI_FLUSH_OUTPUT, false);', 1)
    path.write_text(text.replace(anchor, anchor + fields, 1))
    decoder.write_text(source)
    shutil.copyfile(root/'probe/video/venus-sq2-omx.h', directory/'venus-sq2-omx.h')



def apply_capture_start(directory):
    path = directory/'helpers.c'
    source = path.read_text()
    begin = source.index('void venus_helper_vb2_buf_queue(')
    end = source.index('\n}', begin) + 2
    function = source[begin:end]
    anchor = '\tcache_payload(inst, vb);'
    guard = ('\t/* OMX may STREAMON capture before the decoder has actually started. */\n'
             '\tif (IS_SQ2_HFI(inst->core) &&\n'
             '\t    inst->session_type == VIDC_SESSION_TYPE_DEC &&\n'
             '\t    V4L2_TYPE_IS_CAPTURE(vb->vb2_queue->type) &&\n'
             '\t    !inst->streamon_cap)\n'
             '\t\treturn;\n\n')
    if (function.count(anchor) != 1 or guard in function or
            function.count('v4l2_m2m_buf_queue(m2m_ctx, vbuf);') != 1 or
            function.count('inst->codec_state == VENUS_DEC_STATE_DRC') != 1):
        raise ValueError('Unknown or already modified capture queue start anchor')
    function = function.replace(anchor, guard + anchor, 1)
    path.write_text(source[:begin] + function + source[end:])



def apply_unsubmitted_capture(directory, root):
    path = directory/'helpers.c'
    source = path.read_text()
    begin = source.index('static int session_register_bufs(')
    end = source.index('\n}', begin) + 2
    function = source[begin:end]
    anchor = '\t\tret = hfi_session_set_buffers(inst, &bd);'
    binding = ('\t\tif (IS_SQ2_HFI(core) && inst->session_type == VIDC_SESSION_TYPE_DEC) {\n'
               '\t\t\tret = sq2_venus_scope_bind(inst, buf->vb.vb2_buf.planes[0].mem_priv);\n'
               '\t\t\tif (ret)\n\t\t\t\tbreak;\n\t\t}\n')
    if function.count(anchor) != 1 or binding in function:
        raise ValueError('Unknown or already modified static buffer registration anchor')
    function = function.replace(anchor, binding + anchor, 1)
    path.write_text(source[:begin] + function + source[end:])
    shutil.copyfile(root/'probe/video/venus-sq2-vb2.h', directory/'venus-sq2-vb2.h')


def apply(directory, root):
    def replace(path, before, after):
        s = path.read_text()
        if s.count(before) != 1:
            raise ValueError('Unknown OMX anchor: ' + repr(before))
        path.write_text(s.replace(before, after, 1))
    replace(directory/'hfi.h', 'struct hfi_event_data {\n',
            'struct hfi_event_data {\n\tu32 sq2_sequence_fields;\n')
    for anchor, bit in [
        ('event.height = frame_sz->height;', 1),
        ('event.bit_depth = pixel_depth->bit_depth;', 2),
        ('event.pic_struct = pic_struct->progressive_only;', 4),
        ('event.colour_space = colour_info->colour_space;', 8),
    ]:
        replace(directory/'hfi_msgs.c', anchor, anchor + '\n\t\t\tevent.sq2_sequence_fields |= %d;' % bit)
    p = directory/'vdec.c'
    replace(p, '#include "venus-sq2-close-guard.h"',
            '#include "venus-sq2-close-guard.h"\n#include "venus-sq2-omx.h"')
    replace(p, 'static const struct v4l2_event ev = {\n\t\t.type = V4L2_EVENT_SOURCE_CHANGE,',
            'struct v4l2_event ev = {\n\t\t.type = V4L2_EVENT_SOURCE_CHANGE,')
    replace(p, '\tinst->reconfig = true;\n\tv4l2_event_queue_fh(&inst->fh, &ev);',
            '\tinst->reconfig = true;\n\tif (IS_SQ2_HFI(inst->core))\n\t\tsq2_omx_sequence(&ev, ev_data);\n\tv4l2_event_queue_fh(&inst->fh, &ev);')
    replace(p, '\tret = v4l2_m2m_ioctl_try_decoder_cmd(file, fh, cmd);',
            '\tif (cmd->cmd == 4)\n\t\treturn sq2_omx_flush(inst, cmd->flags);\n\n\tret = v4l2_m2m_ioctl_try_decoder_cmd(file, fh, cmd);')
    shutil.copyfile(root/'probe/video/venus-sq2-omx.h', directory/'venus-sq2-omx.h')

    apply_codec_config(directory, root)
    apply_late_events(directory)
    apply_flush_lock(directory, root)
    apply_capture_start(directory)
    apply_unsubmitted_capture(directory, root)

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory', type=Path)
    p.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    mode = p.add_mutually_exclusive_group()
    mode.add_argument('--codec-config-only', action='store_true')
    mode.add_argument('--late-events-only', action='store_true')
    mode.add_argument('--flush-lock-only', action='store_true')
    mode.add_argument('--capture-start-only', action='store_true')
    mode.add_argument('--unsubmitted-capture-only', action='store_true')
    a = p.parse_args()
    if a.unsubmitted_capture_only:
        apply_unsubmitted_capture(a.directory, a.root)
    elif a.capture_start_only:
        apply_capture_start(a.directory)
    elif a.flush_lock_only:
        apply_flush_lock(a.directory, a.root)
    elif a.late_events_only:
        apply_late_events(a.directory)
    else:
        (apply_codec_config if a.codec_config_only else apply)(a.directory, a.root)
