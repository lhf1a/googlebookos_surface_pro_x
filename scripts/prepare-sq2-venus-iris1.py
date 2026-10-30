#!/usr/bin/env python3
"""Prepare isolated, compile-only Iris1 HFI changes; no platform match or install.

Hardware resource/power and firmware lifetime support are still required.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
HASHES = {
    'hfi_parser.c': '87153ebed2807a08efc1ecd7bf531be3d0d0aff3dca6e136c7fd4091bdf2a327',
    'hfi.c': '1abde9036d7abb7660051233f8d419b495cd3b2dee17b116f69261c65bbef87f',
    'hfi_msgs.c': '4d7f073f9087f6df7d1ec4cb518240fac1b409ba5150fdc1694f0bcc9327b9eb',
    'helpers.h': '99f376ae359a2ad6f3560404ccf8f5dc5721fba48fce81cdd1b1a14a399200a6',
    'vdec.c': '97185064d66075a4ac104915df31c97310f4e8f892aa8cd6f63ce29c87a34c98',
    'core.c': '48f33e0a262e3e70ab3e65cf0ffded6e451a52f5f41c5cf8179cf4da2fd8e922',
    'core.h': '38cfa27196a5204c7250b11ed140821751f6ee8af980715a0ba06db81aabd590',
    'hfi_venus.c': '8323b3366c1044eef997b27f3215d38a8526e56fb3c25b0e0a2fff551ac06064',
    'hfi_venus_io.h': '767743e0ef215aca2fb39470cd3777c8880b14f9ceb41c220ed423c96caf4b7e',
    'firmware.c': 'abd83c9fd854784ddec523bc21d941f3ee5373ca587f19c838e52375cc7c8e71',
    'helpers.c': '86da60ed9373989f7edac92a0d6c375631cae98c4cf8a370d2158382370a1377',
}


def replace(text, before, after):
    if text.count(before) != 1:
        raise ValueError('expected one source anchor: ' + repr(before))
    return text.replace(before, after, 1)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--kernel', type=Path, default=Path('/root/.cache/sq2-thermal/kernel'))
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--firmware-lease', action='store_true',
                   help='compile PAS owner and SQ2 firmware dispatch (platform probe still blocked)')
    p.add_argument('--platform-entry', action='store_true',
                   help='compile opt-in independent core entry; requires firmware lease; no SoC match added')
    p.add_argument('--sq2-resources', action='store_true',
                   help='add only microsoft,sq2-venus-lab match; requires platform entry; runtime allow still defaults false')
    args = p.parse_args()
    if args.platform_entry and not args.firmware_lease:
        p.error('--platform-entry requires --firmware-lease')
    if args.sq2_resources and not args.platform_entry:
        p.error('--sq2-resources requires --platform-entry')
    src = args.kernel / 'drivers/media/platform/qcom/venus'
    for name, digest in HASHES.items():
        if hashlib.sha256((src / name).read_bytes()).hexdigest() != digest:
            raise ValueError('source hash mismatch: ' + name)
    if args.firmware_lease:
        loader = args.kernel / 'drivers/soc/qcom/mdt_loader.c'
        if hashlib.sha256(loader.read_bytes()).hexdigest() != \
                '5dd1d028daafe8006400ca50bc6b7ec54a933521f0f6f1c672445e92a9c3d30b':
            raise ValueError('MDT loader source hash mismatch')
    if args.output.exists():
        raise ValueError('output exists; preserve earlier candidates')
    args.output.mkdir(parents=True)
    for f in src.iterdir():
        if f.is_file() and (f.suffix in ('.c', '.h') or f.name == 'Makefile'):
            shutil.copyfile(f, args.output / f.name)
    shutil.copyfile(ROOT / 'probe/video/venus-iris1.h', args.output / 'venus-iris1.h')
    for name in ('sq2-hfi-properties.h', 'venus-sq2-capabilities.h', 'venus-sq2-frame-sizes.h',
                 'venus-sq2-vp9-eos.h', 'venus-sq2-crop.h'):
        shutil.copyfile(ROOT / 'probe/video' / name, args.output / name)
    parser = (src / 'hfi_parser.c').read_text()
    parser = replace(parser, '#include "hfi_parser.h"',
                     '#include "hfi_parser.h"\n#include "venus-sq2-capabilities.h"')
    parser += ('\nu32 venus_sq2_hfi_capabilities(struct venus_core *core, void *buf, u32 size, u32 count)\n'
               '{\n\treturn venus_sq2_parse_capabilities(core, buf, size, count);\n}\n')
    (args.output / 'hfi_parser.c').write_text(parser)
    with (args.output / 'hfi_parser.h').open('a') as f:
        f.write('\nu32 venus_sq2_hfi_capabilities(struct venus_core *core, void *buf, u32 size, u32 count);\n')
    shutil.copyfile(ROOT / 'probe/video/sq2-venus-dma.h', args.output / 'sq2-venus-dma.h')
    shutil.copyfile(ROOT / 'probe/video/sq2-venus-buffer.h', args.output / 'sq2-venus-buffer.h')
    for name in ('sq2-venus-platform.c', 'sq2-venus-platform.h', 'sq2-venus-lifetime.h',
                 'sq2-venus-power.c', 'sq2-venus-power.h', 'sq2-venus-retire.c',
                 'sq2-venus-retire.h', 'venus-sq2-vb2.h', 'venus-sq2-intbuf.h',
                 'venus-sq2-callback.h', 'venus-sq2-session-end.h'):
        shutil.copyfile(ROOT / 'probe/video' / name, args.output / name)
    with (args.output / 'Makefile').open('a') as f:
        f.write('\nvenus-core-objs += sq2-venus-platform.o sq2-venus-power.o sq2-venus-retire.o\n')
    core = (src / 'core.h').read_text()
    core = replace(core, 'struct venus_inst {',
                   'struct venus_inst {\n\tatomic_t sq2_callbacks;\n\tbool sq2_closing;\n'
                   '\tstruct completion sq2_end_done;\n\tu32 sq2_end_error;\n'
                   '\tstruct sq2_venus_scope *sq2_scope;\n'
                   '\tbool sq2_end_sent, sq2_end_confirmed;')
    core = replace(core, 'struct venus_core {',
                   'struct sq2_venus_fw;\nstruct sq2_venus_platform;\nstruct venus_core {\n'
                   '\tstruct sq2_venus_platform *sq2_platform;\n'
                   '\tstruct sq2_venus_fw *sq2_fw_owner;\n'
                   '\tbool sq2_public, sq2_children, sq2_pm_enabled;\n'
                   '\tstruct mutex sq2_retire_lock;\n'
                   '\tunsigned long sq2_vb2_bytes;\n'
                   '\tstruct list_head sq2_retired;\n'
                   '\tbool sq2_dma_stopped;\n'
                   '\tbool sq2_boot_attempted, sq2_secure_active;')
    core = replace(core, '\tenum hfi_version hfi_version;\n',
                   '\t/* VPU hardware is independent of HFI packet version. */\n'
                   '\tbool iris1;\n\tbool sq2_windows_hfi;\n\tenum hfi_version hfi_version;\n')
    core = replace(core, '#define IS_V4(core)',
                   '#define IS_SQ2_HFI(core) ((core)->res->sq2_windows_hfi)\n'
                   '#define IS_IRIS1(core) ((core)->res->iris1)\n#define IS_V4(core)')
    # Keep all pre-existing SoC tables unchanged (zero-initialized iris1=false).
    (args.output / 'core.h').write_text(core)
    vdec = (src / 'vdec.c').read_text()
    # Append: vdec_init_instance pins existing array indices for AVC and NV12.
    anchor = '\t\t.pixfmt = V4L2_PIX_FMT_HEVC,\n\t\t.num_planes = 1,\n\t\t.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,\n\t\t.flags = V4L2_FMT_FLAG_DYN_RESOLUTION,'
    vdec = replace(vdec, anchor, anchor + '\n\t}, {\n\t\t.pixfmt = V4L2_PIX_FMT_P010,\n'
                   '\t\t.num_planes = 1,\n\t\t.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,')
    anchor = '\tif (V4L2_TYPE_IS_CAPTURE(type) && fmt[i].pixfmt == V4L2_PIX_FMT_QC10C &&'
    vdec = replace(vdec, anchor,
                   '\tif (fmt[i].pixfmt == V4L2_PIX_FMT_P010 &&\n'
                   '\t    (!IS_SQ2_HFI(inst->core) || inst->bit_depth != VIDC_BITDEPTH_10))\n'
                   '\t\treturn NULL;\n\n' + anchor)
    anchor = '\t\t\tvalid = venus_helper_check_format(inst, fmt[i].pixfmt);'
    vdec = replace(vdec, anchor, anchor + '\n\t\t\tif (fmt[i].pixfmt == V4L2_PIX_FMT_P010 &&\n'
                   '\t\t\t    (!IS_SQ2_HFI(inst->core) || inst->bit_depth != VIDC_BITDEPTH_10))\n'
                   '\t\t\t\tvalid = false;')
    vdec = replace(vdec, '\t\tpfmt[0].bytesperline = ALIGN(pixmp->width, 128);',
                   '\t\tpfmt[0].bytesperline = pixmp->pixelformat == V4L2_PIX_FMT_P010 ?\n'
                   '\t\t\tALIGN(pixmp->width * 2, 128) : ALIGN(pixmp->width, 128);')
    vdec = replace(vdec, '#include "hfi_parser.h"',
                   '#include "hfi_parser.h"\n#include "venus-sq2-frame-sizes.h"\n#include "venus-sq2-crop.h"')
    anchor = '\tif (fsize->index)\n\t\treturn -EINVAL;'
    vdec = replace(vdec, anchor,
                   '\tif (IS_SQ2_HFI(inst->core) && fmt->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)\n'
                   '\t\treturn venus_sq2_enum_sizes(inst->core, fsize);\n\n' + anchor)
    # Preserve SOURCE_CHANGE crop only for a same-size capture negotiation.
    anchor = '\tu32 pixfmt_out = 0, pixfmt_cap = 0;'
    vdec = replace(vdec, anchor, anchor + '\n\tbool keep_crop;')
    anchor = '\tinst->width = format.fmt.pix_mp.width;\n\tinst->height = format.fmt.pix_mp.height;\n\tinst->crop.top = 0;\n\tinst->crop.left = 0;\n\tinst->crop.width = inst->width;\n\tinst->crop.height = inst->height;'
    vdec = replace(vdec, anchor,
                   '\tkeep_crop = venus_sq2_keep_crop(inst, f->type,\n'
                   '\t\tformat.fmt.pix_mp.width, format.fmt.pix_mp.height);\n'
                   '\tinst->width = format.fmt.pix_mp.width;\n'
                   '\tinst->height = format.fmt.pix_mp.height;\n'
                   '\tif (!keep_crop) {\n\t\tinst->crop.top = 0;\n\t\tinst->crop.left = 0;\n'
                   '\t\tinst->crop.width = inst->width;\n\t\tinst->crop.height = inst->height;\n\t}')
    # The SQ2 VB2 owner takes an independent exporter reference. dma_buf_put
    # is exported in DMA_BUF; missing Module.symvers can hide this at modpost.
    vdec += '\nMODULE_IMPORT_NS(DMA_BUF);\n'
    # A zero-length LAST buffer is the drain terminator, not a decoded frame.
    # Preserve legacy behavior and the later DATACORRUPT/DROP error checks.
    vdec = replace(vdec, '\t\tif (!bytesused)\n\t\t\tstate = VB2_BUF_STATE_ERROR;',
                   '\t\tif (!bytesused && (!IS_SQ2_HFI(inst->core) ||\n'
                   '\t\t    !(vbuf->flags & V4L2_BUF_FLAG_LAST)))\n'
                   '\t\t\tstate = VB2_BUF_STATE_ERROR;')
    trace = '''
static bool sq2_stage_trace;
module_param(sq2_stage_trace, bool, 0400);
static atomic_t sq2_stage_records = ATOMIC_INIT(0);
static void sq2_vdec_trace(struct venus_inst *inst, const char *stage, int ret)
{
 if (!sq2_stage_trace || !IS_SQ2_HFI(inst->core)) return;
 if (!atomic_add_unless(&sq2_stage_records, 1, 128)) return;
 dev_info(inst->core->dev, "SQ2_VDEC_STAGE %s ret=%d\\n", stage, ret);
}
'''
    vdec = replace(vdec, '#include "pm_helpers.h"', '#include "pm_helpers.h"\n' + trace)
    # Observe the first unfinished native path without adding MMIO, delays,
    # retries or changing any call/return value. Explicit opt-in, max 128 logs.
    for first, last, calls in (
        ('static int vdec_session_init(', 'static int vdec_num_buffers(', (
            ('session_init', 'ret = venus_helper_session_init(inst);'),
            ('input_resolution', 'ret = venus_helper_set_input_resolution(inst, frame_width_min(inst),\n\t\t\t\t\t\tframe_height_min(inst));'))),
        ('static int vdec_num_buffers(', 'static int vdec_queue_setup(', (
            ('input_bufreq', 'ret = venus_helper_get_bufreq(inst, HFI_BUFFER_INPUT, &bufreq);'),
            ('output_bufreq', 'ret = venus_helper_get_bufreq(inst, HFI_BUFFER_OUTPUT, &bufreq);'))),
        ('static int vdec_queue_setup(', 'static int vdec_verify_conf(', (
            ('queue_pm_get', 'ret = vdec_pm_get(inst);'),
            ('queue_session', 'ret = vdec_session_init(inst);'),
            ('queue_counts', 'ret = vdec_num_buffers(inst, &in_num, &out_num);'),
            ('queue_pm_put', 'ret = vdec_pm_put(inst, false);'))),
    ):
        begin, finish = vdec.index(first), vdec.index(last)
        part = vdec[begin:finish]
        for stage, call in calls:
            part = replace(part, '\t' + call,
                           f'\tsq2_vdec_trace(inst, "{stage}_begin", 0);\n\t' + call +
                           f'\n\tsq2_vdec_trace(inst, "{stage}_end", ret);')
        vdec = vdec[:begin] + part + vdec[finish:]
    start, end = vdec.index('static void vdec_stop_streaming('), vdec.index('static void vdec_session_release(')
    section = replace(vdec[start:end], '\tinst->session_error = 0;',
                      '\tif (IS_SQ2_HFI(inst->core)) {\n'
                      '\t\tif (ret || test_bit(0, &inst->core->sys_error))\n'
                      '\t\t\tinst->session_error = true;\n'
                      '\t} else inst->session_error = 0;')
    vdec = vdec[:start] + section + vdec[end:]
    start, end = vdec.index('static int vdec_start_streaming('), vdec.index('static void vdec_cancel_dst_buffers(')
    section = replace(vdec[start:end], '\tmutex_lock(&inst->lock);',
                      '\tmutex_lock(&inst->lock);\n'
                      '\tif (IS_SQ2_HFI(inst->core) && (inst->session_error ||\n'
                      '\t    test_bit(0, &inst->core->sys_error))) {\n'
                      '\t\tret = -EIO;\n\t\tgoto error;\n\t}\n')
    vdec = vdec[:start] + section + vdec[end:]
    start, end = vdec.index('static void vdec_session_release('), vdec.index('static int vdec_buf_init(')
    section = vdec[start:end]
    anchor = '\tabort = (ret && ret != -EINVAL) ? 1 : 0;'
    if section.count(anchor) != 5:
        raise ValueError('expected five session-release error decisions')
    section = section.replace(anchor,
                              '\tif (IS_SQ2_HFI(core)) abort |= (ret && ret != -EINVAL);\n'
                              '\telse abort = (ret && ret != -EINVAL) ? 1 : 0;')
    vdec = vdec[:start] + section + vdec[end:]
    pos = vdec.index('static int vdec_close(')
    vdec = vdec[:pos] + replace(vdec[pos:], '\tvdec_pm_get(inst);',
                               '\tsq2_callback_close_begin(inst);\n\tvdec_pm_get(inst);')
    vdec = replace(vdec, '\thfi_session_destroy(inst);\n\tmutex_destroy(&inst->lock);',
                   '\thfi_session_destroy(inst);\n'
                   '\tvenus_helper_sq2_retire_buffers(inst);\n'
                   '\tmutex_destroy(&inst->lock);')
    vdec = replace(vdec, '#include "pm_helpers.h"',
                   '#include "pm_helpers.h"\n#include "venus-sq2-vb2.h"\n#include "venus-sq2-callback.h"')
    for queue in ('src_vq', 'dst_vq'):
        vdec = replace(vdec, f'\t{queue}->mem_ops = &vb2_dma_contig_memops;',
                       f'\t{queue}->mem_ops = IS_SQ2_HFI(inst->core) ?\n'
                       '\t\t&sq2_vb2_memops : &vb2_dma_contig_memops;')
    (args.output / 'vdec.c').write_text(vdec)
    session = (src / 'hfi.c').read_text()
    session = replace(session, 'static u32 to_codec_type(',
                      '#include "venus-sq2-session-end.h"\n\nstatic u32 to_codec_type(')
    session = replace(session, '#include "core.h"',
                      '#include "core.h"\n#include "sq2-venus-retire.h"\n#include "venus-sq2-callback.h"')
    session = replace(session, '\tinit_completion(&inst->done);',
                      '\tatomic_set(&inst->sq2_callbacks, 0);\n'
                      '\tinst->sq2_closing = false;\n'
                      '\tinit_completion(&inst->sq2_end_done);\n'
                      '\tinst->sq2_end_sent = inst->sq2_end_confirmed = false;\n'
                      '\tinit_completion(&inst->done);')
    session = replace(session, '\tinst->hfi_codec = to_codec_type(pixfmt);',
                      '\tif (IS_SQ2_HFI(core)) {\n'
                      '\t\tif (inst->sq2_end_sent && !inst->sq2_end_confirmed) return -EIO;\n'
                      '\t\tret = sq2_venus_scope_begin(inst);\n\t\tif (ret) return ret;\n'
                      '\t\tinst->sq2_end_sent = inst->sq2_end_confirmed = false;\n\t}\n'
                      '\tinst->hfi_codec = to_codec_type(pixfmt);')
    start = session.index('int hfi_session_deinit(')
    end = session.index('EXPORT_SYMBOL_GPL(hfi_session_deinit);', start)
    section = replace(session[start:end], '\tint ret;',
                      '\tint ret;\n\n\tif (IS_SQ2_HFI(inst->core)) return sq2_session_end(inst);')
    session = session[:start] + section + session[end:]
    start = session.index('void hfi_session_destroy(')
    end = session.index('EXPORT_SYMBOL_GPL(hfi_session_destroy);', start)
    section = replace(session[start:end], '\tmutex_unlock(&core->lock);',
                      '\tmutex_unlock(&core->lock);\n\tsq2_callback_drain(inst);\n'
                      '\tif (IS_SQ2_HFI(core)) sq2_venus_scope_drop(inst);')
    session = session[:start] + section + session[end:]
    (args.output / 'hfi.c').write_text(session)
    msgs = (src / 'hfi_msgs.c').read_text()
    msgs = replace(msgs, '#include "hfi_parser.h"',
                   '#include "hfi_parser.h"\n#include "venus-sq2-vp9-eos.h"')
    anchor = '\t\terror = pkt->error_type;\n\t} else {\n\t\terror = HFI_ERR_SESSION_INVALID_PARAMETER;'
    msgs = replace(msgs, anchor,
                   '\t\terror = pkt->error_type;\n'
                   '\t\thfi_flags = venus_sq2_vp9_eos_flags(\n'
                   '\t\t\tIS_SQ2_HFI(core) && inst->hfi_codec == HFI_VIDEO_CODEC_VP9,\n'
                   '\t\t\terror, filled_len, offset, hfi_flags);\n'
                   '\t} else {\n\t\terror = HFI_ERR_SESSION_INVALID_PARAMETER;')
    # Only SYS_INIT_DONE carries the complete capability table. Session init
    # keeps upstream handling; its shorter payload must not replace core caps.
    anchor = '\terror = hfi_parser(core, inst, pkt->data, rem_bytes);'
    if msgs.count(anchor) != 2:
        raise ValueError('expected system and session capability parser calls')
    msgs = msgs.replace(anchor,
        '\tif (IS_SQ2_HFI(core))\n'
        '\t\terror = venus_sq2_hfi_capabilities(core, pkt->data, rem_bytes, pkt->num_properties);\n'
        '\telse\n\t' + anchor, 1)
    start = msgs.index('static void hfi_session_end_done(')
    end = msgs.index('static void hfi_session_abort_done(', start)
    section = replace(msgs[start:end], '\tinst->error = pkt->error_type;',
                      '\tif (IS_SQ2_HFI(core)) {\n'
                      '\t\tinst->sq2_end_error = pkt->error_type;\n'
                      '\t\tcomplete(&inst->sq2_end_done);\n\t\treturn;\n\t}\n'
                      '\tinst->error = pkt->error_type;')
    msgs = msgs[:start] + section + msgs[end:]
    # Include after packet constants have been declared.
    pos = msgs.index('struct hfi_done_handler')
    msgs = msgs[:pos] + '#include "venus-sq2-callback.h"\n\n' + msgs[pos:]
    msgs = replace(msgs, 'to_instance(struct venus_core *core, u32 session_id)',
                   'to_instance(struct venus_core *core, u32 session_id, u32 packet)')
    msgs = replace(msgs, '\t\tif (hash32_ptr(inst) == session_id) {',
                   '\t\tif (hash32_ptr(inst) == session_id) {\n'
                   '\t\t\tif (!sq2_callback_get(inst, packet)) break;')
    msgs = replace(msgs, 'to_instance(core, pkt->shdr.session_id);',
                   'to_instance(core, pkt->shdr.session_id, hdr->pkt_type);')
    msgs = replace(msgs, '\thandler->done(core, inst, hdr);',
                   '\thandler->done(core, inst, hdr);\n\tsq2_callback_put(inst);')
    msgs = replace(msgs, '\tif (inst->ops->flush_done)',
                   '\tif (inst->ops->flush_done &&\n'
                   '\t    (!IS_SQ2_HFI(core) || !READ_ONCE(inst->sq2_closing)))')
    (args.output / 'hfi_msgs.c').write_text(msgs)
    platform = (src / 'core.c').read_text()
    platform = replace(platform, '\t\tinst->ops->event_notify(inst, EVT_SESSION_ERROR, NULL);',
                       '\t\tif (!IS_SQ2_HFI(core) || !READ_ONCE(inst->sq2_closing))\n'
                       '\t\t\tinst->ops->event_notify(inst, EVT_SESSION_ERROR, NULL);')
    platform = replace(platform, '\tmutex_init(&core->pm_lock);',
                       '\t/* PAS callsites are compile-tested; devres quarantine is pending. */\n'
                       '\tif (IS_SQ2_HFI(core))\n\t\treturn -EOPNOTSUPP;\n\n'
                       '\tmutex_init(&core->pm_lock);')
    if args.platform_entry:
        shutil.copyfile(ROOT / 'probe/video/venus-sq2-core.h', args.output / 'venus-sq2-core.h')
        platform = replace(platform, 'static int venus_probe(struct platform_device *pdev)',
                           '#include "venus-sq2-core.h"\n\nstatic int venus_probe(struct platform_device *pdev)')
        platform = replace(platform, '\tcore = devm_kzalloc(dev, sizeof(*core), GFP_KERNEL);',
                           '\tconst struct venus_resources *res = of_device_get_match_data(dev);\n\n'
                           '\tif (res && res->sq2_windows_hfi)\n\t\treturn venus_sq2_probe(pdev, res);\n\n'
                           '\tcore = devm_kzalloc(dev, sizeof(*core), GFP_KERNEL);')
        platform = replace(platform, '\tdisable_irq_nosync(core->irq);\n\tschedule_delayed_work',
                           '\tif (IS_SQ2_HFI(core)) {\n'
                           '\t\tschedule_delayed_work(&core->work, 0);\n\t\treturn;\n\t}\n'
                           '\tdisable_irq_nosync(core->irq);\n\tschedule_delayed_work')
        platform = replace(platform, '\tret = pm_runtime_get_sync(dev);\n\tWARN_ON(ret < 0);',
                           '\tif (IS_SQ2_HFI(core))\n\t\treturn venus_sq2_remove(pdev);\n\n'
                           '\tret = pm_runtime_get_sync(dev);\n\tWARN_ON(ret < 0);')
        platform = replace(platform, '\tpm_runtime_get_sync(core->dev);\n\tvenus_shutdown(core);',
                           '\tif (IS_SQ2_HFI(core)) {\n'
                           '\t\tcancel_delayed_work_sync(&core->work);\n'
                           '\t\tvenus_sq2_stop_core(core);\n\t\treturn;\n\t}\n'
                           '\tpm_runtime_get_sync(core->dev);\n\tvenus_shutdown(core);')
        platform = replace(platform, '\tret = hfi_core_suspend(core);',
                           '\tif (IS_SQ2_HFI(core))\n\t\treturn -EBUSY;\n'
                           '\tret = hfi_core_suspend(core);')
        platform = replace(platform, '\tret = icc_set_bw(core->video_path, kbps_to_icc(20000), 0);',
                           '\tif (IS_SQ2_HFI(core))\n'
                           '\t\treturn test_bit(0, &core->sys_error) ? -EIO : 0;\n'
                           '\tret = icc_set_bw(core->video_path, kbps_to_icc(20000), 0);')
    if args.sq2_resources:
        shutil.copyfile(ROOT / 'probe/video/venus-sq2-resources.h', args.output / 'venus-sq2-resources.h')
        platform = replace(platform, 'static const struct of_device_id venus_dt_match[] = {',
                           '#include "venus-sq2-resources.h"\n'
                           'static const struct of_device_id venus_dt_match[] = {\n'
                           '\t{ .compatible = "microsoft,sq2-venus-lab", .data = &sq2_lab_res },')
    (args.output / 'core.c').write_text(platform)
    hfi = (src / 'hfi_venus.c').read_text()
    hfi = replace(hfi, '#include <linux/slab.h>',
                  '#include <linux/slab.h>\n#include <linux/iommu.h>\n#include "sq2-venus-dma.h"')
    hfi = replace(hfi, '\tstruct venus_core *core;\n',
                  '\tstruct venus_core *core;\n\tstruct sq2_venus_dma sq2_dma;\n')
    hfi = replace(hfi, '#include "firmware.h"', '#include "firmware.h"\n#include "venus-iris1.h"')
    hfi = replace(hfi, '\tif (IS_V6(hdev->core)) {\n\t\tmask_val = readl(wrapper_base + WRAPPER_INTR_MASK);',
                  '\tif (IS_IRIS1(hdev->core)) {\n'
                  '\t\tmask_val = venus_iris1_irq_mask(readl(wrapper_base + WRAPPER_INTR_MASK));\n'
                  '\t} else if (IS_V6(hdev->core)) {\n\t\tmask_val = readl(wrapper_base + WRAPPER_INTR_MASK);')
    hfi = replace(hfi, '\tvenus_set_registers(hdev);',
                  '\tif (IS_IRIS1(hdev->core))\n\t\tvenus_iris1_clock_config(hdev->core);\n'
                  '\tvenus_set_registers(hdev);')
    hfi = replace(hfi, '\tret = venus_boot_core(hdev);',
                  '\tif (IS_IRIS1(hdev->core) && !IS_SQ2_HFI(hdev->core))\n'
                  '\t\tvenus_iris1_dsp_map(hdev->core, hdev->ifaceq_table.da, SHARED_QSIZE);\n\n'
                  '\tret = venus_boot_core(hdev);')
    # SQ2 has one coherent UC allocation: SFR is a view, not a second owner.
    hfi = replace(hfi, '\tdesc->attrs = DMA_ATTR_WRITE_COMBINE;',
                  '\tdesc->attrs = IS_SQ2_HFI(hdev->core) ? 0 : DMA_ATTR_WRITE_COMBINE;')
    hfi = replace(hfi, '\tvenus_free(hdev, &hdev->sfr);',
                  '\tif (!IS_SQ2_HFI(hdev->core))\n\t\tvenus_free(hdev, &hdev->sfr);')
    hfi = replace(hfi, '\tret = venus_alloc(hdev, &desc, ALIGNED_QUEUE_SIZE);',
                  '\tret = venus_alloc(hdev, &desc, IS_SQ2_HFI(hdev->core) ?\n'
                  '\t\tSQ2_VENUS_UC_BYTES : ALIGNED_QUEUE_SIZE);')
    hfi = replace(hfi, '\thdev->ifaceq_table = desc;',
                  '\tif (IS_SQ2_HFI(hdev->core)) {\n'
                  '\t\tret = sq2_venus_dma_check_uc(hdev->core->dev, desc.da, desc.size);\n'
                  '\t\tif (ret) {\n'
                  '\t\t\tvenus_free(hdev, &desc);\n\t\t\treturn ret;\n\t\t}\n'
                  '\t\tmemset(desc.kva, 0, desc.size);\n\t}\n'
                  '\thdev->ifaceq_table = desc;')
    hfi = replace(hfi, '\tret = venus_alloc(hdev, &desc, ALIGNED_SFR_SIZE);',
                  '\tif (IS_SQ2_HFI(hdev->core)) {\n'
                  '\t\tdesc = hdev->ifaceq_table;\n'
                  '\t\tdesc.da += ALIGNED_QUEUE_SIZE;\n'
                  '\t\tdesc.kva += ALIGNED_QUEUE_SIZE;\n'
                  '\t\tdesc.size = ALIGNED_SFR_SIZE;\n\t\tret = 0;\n'
                  '\t} else {\n\t\tret = venus_alloc(hdev, &desc, ALIGNED_SFR_SIZE);\n\t}')
    start = '\twritel(hdev->ifaceq_table.da, cpu_cs_base + UC_REGION_ADDR);'
    end = '\t\twritel(hdev->sfr.da, cpu_cs_base + SFR_ADDR);'
    legacy = hfi[hfi.index(start):hfi.index(end) + len(end)]
    hfi = replace(hfi, legacy,
                  '\tif (IS_SQ2_HFI(hdev->core)) {\n'
                  '\t\tvenus_sq2_cpu_map(hdev->core, hdev->ifaceq_table.da, hdev->sfr.da);\n'
                  '\t} else {\n' + legacy + '\n\t}')
    hfi = replace(hfi, '\twritel(1, cpu_cs_base + CPU_CS_SCIACMDARG3);',
                  '\tif (!IS_SQ2_HFI(hdev->core))\n'
                  '\t\twritel(1, cpu_cs_base + CPU_CS_SCIACMDARG3);')
    hfi = replace(hfi, '\thdev = kzalloc(sizeof(*hdev), GFP_KERNEL);',
                  '\t/* No implicit activation on existing HFI4 platforms. */\n'
                  '\tif (IS_SQ2_HFI(core) && (!IS_IRIS1(core) || !IS_V4(core)))\n'
                  '\t\treturn -EINVAL;\n\n'
                  '\thdev = kzalloc(sizeof(*hdev), GFP_KERNEL);')
    hfi = replace(hfi, '\tret = venus_interface_queues_init(hdev);',
                  '\tif (IS_SQ2_HFI(core)) {\n'
                  '\t\tret = sq2_venus_dma_begin(core->dev, &hdev->sq2_dma);\n'
                  '\t\tif (ret)\n\t\t\tgoto err_kfree;\n\t}\n'
                  '\tret = venus_interface_queues_init(hdev);')
    hfi = replace(hfi, 'err_kfree:\n\tkfree(hdev);',
                  'err_kfree:\n\tsq2_venus_dma_restore(core->dev, &hdev->sq2_dma);\n'
                  '\tmutex_destroy(&hdev->lock);\n\tkfree(hdev);')
    hfi = replace(hfi, '\tvenus_interface_queues_release(hdev);',
                  '\tvenus_interface_queues_release(hdev);\n'
                  '\tsq2_venus_dma_restore(core->dev, &hdev->sq2_dma);')
    hfi = replace(hfi, '\tret = venus_sys_set_power_control(hdev, venus_fw_low_power_mode);',
                  '\tret = venus_sys_set_power_control(hdev, IS_SQ2_HFI(hdev->core) ?\n'
                  '\t\tfalse : venus_fw_low_power_mode);')
    start = hfi.index('static int venus_core_deinit(')
    end = hfi.index('static int venus_core_ping(', start)
    section = replace(hfi[start:end], '\tvenus_set_state(hdev, VENUS_STATE_DEINIT);',
                      '\tif (IS_SQ2_HFI(core)) {\n'
                      '\t\tmutex_lock(&hdev->lock);\n'
                      '\t\thdev->state = VENUS_STATE_DEINIT;\n'
                      '\t\thdev->suspended = true;\n'
                      '\t\thdev->power_enabled = false;\n'
                      '\t\tmutex_unlock(&hdev->lock);\n\t\treturn 0;\n\t}\n'
                      '\tvenus_set_state(hdev, VENUS_STATE_DEINIT);')
    hfi = hfi[:start] + section + hfi[end:]
    start = hfi.index('static int venus_resume(')
    end = hfi.index('static int venus_suspend_1xx(', start)
    section = replace(hfi[start:end], '\tif (!hdev->suspended)',
                      '\t/* Serialize against terminal deinit before any power/MMIO access. */\n'
                      '\tif (IS_SQ2_HFI(core) && (!venus_is_valid_state(hdev) ||\n'
                      '\t    test_bit(0, &core->sys_error))) {\n'
                      '\t\tret = -EIO;\n\t\tgoto unlock;\n\t}\n\n'
                      '\tif (!hdev->suspended)')
    hfi = hfi[:start] + section + hfi[end:]
    (args.output / 'hfi_venus.c').write_text(hfi)
    helpers = (src / 'helpers.c').read_text()
    anchor = '\tcase V4L2_PIX_FMT_NV12:\n\t\treturn HFI_COLOR_FORMAT_NV12;'
    helpers = replace(helpers, anchor, '\tcase V4L2_PIX_FMT_P010:\n\t\treturn HFI_COLOR_FORMAT_P010;\n' + anchor)
    anchor = '\tif (ubwc) {\n\t\tubwc_fmt = fmt | HFI_COLOR_FORMAT_UBWC_BASE;'
    helpers = replace(helpers, anchor,
                      '\t/* SQ2 advertises TP10 UBWC DPB + linear P010 output2. */\n'
                      '\tif (IS_SQ2_HFI(core) && fmt == HFI_COLOR_FORMAT_P010) {\n'
                      '\t\tif (!find_fmt_from_caps(caps, HFI_BUFFER_OUTPUT, HFI_COLOR_FORMAT_YUV420_TP10_UBWC) ||\n'
                      '\t\t    !find_fmt_from_caps(caps, HFI_BUFFER_OUTPUT2, HFI_COLOR_FORMAT_P010))\n'
                      '\t\t\treturn -EINVAL;\n'
                      '\t\t*out_fmt = HFI_COLOR_FORMAT_YUV420_TP10_UBWC;\n'
                      '\t\t*out2_fmt = HFI_COLOR_FORMAT_P010;\n\t\treturn 0;\n\t}\n\n' + anchor)
    start, end = helpers.index('int venus_helper_free_dpb_bufs('), helpers.index('EXPORT_SYMBOL_GPL(venus_helper_free_dpb_bufs);')
    section = replace(helpers[start:end], '\t\tif (buf->owned_by == FIRMWARE)',
                      '\t\tif (buf->owned_by == FIRMWARE &&\n'
                      '\t\t    (!IS_SQ2_HFI(inst->core) ||\n'
                      '\t\t     !sq2_venus_scope_ended(inst, &buf->sq2_retired)))')
    helpers = helpers[:start] + section + helpers[end:]
    # Bind at actual publication, not allocation: VB2 objects can survive END.
    for begin, finish, anchor, entry in (
        ('int venus_helper_queue_dpb_bufs(', 'EXPORT_SYMBOL_GPL(venus_helper_queue_dpb_bufs);',
         '\t\tret = hfi_session_process_buf(inst, &fdata);', '&buf->sq2_retired'),
        ('session_process_buf(struct venus_inst *inst,', 'static bool is_dynamic_bufmode(',
         '\tret = hfi_session_process_buf(inst, &fdata);', 'vb->planes[0].mem_priv'),
    ):
        start, end = helpers.index(begin), helpers.index(finish)
        section = replace(helpers[start:end], anchor,
                          '\tif (IS_SQ2_HFI(inst->core)) {\n'
                          f'\t\tret = sq2_venus_scope_bind(inst, {entry});\n'
                          '\t\tif (ret) return ret;\n\t}\n' + anchor)
        helpers = helpers[:start] + section + helpers[end:]
    start, end = helpers.index('static int intbufs_set_buffer('), helpers.index('static int intbufs_unset_buffers(')
    section = replace(helpers[start:end], '\t\tret = hfi_session_set_buffers(inst, &bd);',
                      '\t\tif (IS_SQ2_HFI(core)) {\n'
                      '\t\t\tret = sq2_venus_scope_bind(inst, &buf->sq2_retired);\n'
                      '\t\t\tif (ret) goto dma_free;\n\t\t}\n'
                      '\t\tret = hfi_session_set_buffers(inst, &bd);')
    section = replace(section, 'dma_free:\n',
                      'dma_free:\n\tif (IS_SQ2_HFI(core) && buf->sq2_retired.scope) {\n'
                      '\t\tsq2_intbuf_retire(inst, buf);\n\t\treturn ret;\n\t}\n')
    helpers = helpers[:start] + section + helpers[end:]
    helper_h = (src / 'helpers.h').read_text()
    helper_h = replace(helper_h, '\n#endif',
                       '\nvoid venus_helper_sq2_retire_buffers(struct venus_inst *inst);\n#endif')
    (args.output / 'helpers.h').write_text(helper_h)
    helpers = replace(helpers, 'struct intbuf {',
                      'struct intbuf {\n\tstruct sq2_venus_retired sq2_retired;\n'
                      '\tstruct venus_core *sq2_core;\n\tunsigned long sq2_charged;\n'
                      '\tstruct device *sq2_dev;')
    helpers = replace(helpers, 'bool venus_helper_check_codec(',
                      '#include "venus-sq2-intbuf.h"\n\nbool venus_helper_check_codec(')
    helpers = replace(helpers, '\tida_free(&inst->dpb_ids, buf->dpb_out_tag);',
                      '\tif (IS_SQ2_HFI(inst->core)) {\n'
                      '\t\t/* Do not recycle a tag still visible to live firmware. */\n'
                      '\t\tsq2_intbuf_retire(inst, buf);\n\t\treturn;\n\t}\n'
                      '\tida_free(&inst->dpb_ids, buf->dpb_out_tag);')
    # An unsuccessful RELEASE_BUFFERS is not a DMA ownership transfer.
    # Keep the failed buffer and the unprocessed tail linked, and preserve the
    # first error. This does not replace the pending global PAS/VB2 quarantine.
    for begin, finish in (
        ('static int intbufs_unset_buffers(', 'static const unsigned int intbuf_types_1xx'),
        ('int venus_helper_intbufs_realloc(', 'EXPORT_SYMBOL_GPL(venus_helper_intbufs_realloc);'),
    ):
        start, end = helpers.index(begin), helpers.index(finish)
        section = replace(helpers[start:end],
                          '\t\tret = hfi_session_unset_buffers(inst, &bd);',
                          '\t\tret = hfi_session_unset_buffers(inst, &bd);\n'
                          '\t\tif (IS_SQ2_HFI(inst->core) && ret)\n'
                          '\t\t\treturn ret; /* no confirmed release: retain DMA and list entry */\n'
                          '\t\tif (IS_SQ2_HFI(inst->core)) {\n'
                          '\t\t\tsq2_intbuf_retire(inst, buf);\n\t\t\tcontinue;\n\t\t}')
        helpers = helpers[:start] + section + helpers[end:]
    helpers = replace(helpers, '#include "core.h"',
                      '#include "core.h"\n#include "sq2-venus-buffer.h"\n#include "sq2-venus-retire.h"')
    helpers = replace(helpers, '\t\tbuf->owned_by = DRIVER;',
                      '\t\tif (IS_SQ2_HFI(inst->core) && !sq2_venus_buffer_valid(buf->da, buf->size)) {\n'
                      '\t\t\tdma_free_attrs(dev, buf->size, buf->va, buf->da, buf->attrs);\n'
                      '\t\t\tret = -ERANGE;\n\t\t\tgoto fail;\n\t\t}\n'
                      '\t\tbuf->owned_by = DRIVER;')
    helpers = replace(helpers, '\t\tif (id < 0) {\n\t\t\tret = id;',
                      '\t\tif (id < 0) {\n'
                      '\t\t\tif (IS_SQ2_HFI(inst->core))\n'
                      '\t\t\t\tdma_free_attrs(dev, buf->size, buf->va, buf->da, buf->attrs);\n'
                      '\t\t\tret = id;')
    start = helpers.index('static int intbufs_set_buffer(')
    end = helpers.index('static int intbufs_unset_buffers(')
    section = replace(helpers[start:end], '\t\tmemset(&bd, 0, sizeof(bd));',
                      '\t\tif (IS_SQ2_HFI(core) && !sq2_venus_buffer_valid(buf->da, buf->size)) {\n'
                      '\t\t\tret = -ERANGE;\n\t\t\tgoto dma_free;\n\t\t}\n'
                      '\t\tmemset(&bd, 0, sizeof(bd));')
    helpers = helpers[:start] + section + helpers[end:]
    helpers = replace(helpers, '\tbuf->size = vb2_plane_size(vb, 0);',
                      '\tif (IS_SQ2_HFI(inst->core) && (vb->num_planes != 1 ||\n'
                      '\t    !sq2_venus_buffer_valid(vb2_dma_contig_plane_dma_addr(vb, 0),\n'
                      '\t                            vb2_plane_size(vb, 0))))\n'
                      '\t\treturn -ERANGE;\n'
                      '\tbuf->size = vb2_plane_size(vb, 0);')
    anchor = 'int venus_helper_vb2_buf_prepare(struct vb2_buffer *vb)'
    pos = helpers.index(anchor)
    before, prepare = helpers[:pos], helpers[pos:]
    prepare = replace(prepare, '\tif (V4L2_TYPE_IS_OUTPUT(vb->vb2_queue->type)) {',
                      '\tif (IS_SQ2_HFI(inst->core) && (vb->num_planes != 1 ||\n'
                      '\t    !sq2_venus_buffer_valid(vb2_dma_contig_plane_dma_addr(vb, 0),\n'
                      '\t                            vb2_plane_size(vb, 0))))\n'
                      '\t\treturn -ERANGE;\n'
                      '\tif (IS_SQ2_HFI(inst->core) && V4L2_TYPE_IS_OUTPUT(vb->type) &&\n'
                      '\t    !sq2_venus_payload_valid(vb2_plane_size(vb, 0),\n'
                      '\t                             vb2_get_plane_payload(vb, 0),\n'
                      '\t                             vb->planes[0].data_offset))\n'
                      '\t\treturn -EINVAL;\n'
                      '\tif (V4L2_TYPE_IS_OUTPUT(vb->vb2_queue->type)) {')
    helpers = before + prepare
    helpers, alloc_count = re.subn(
        r'dma_alloc_attrs\(dev, buf->size, &buf->da, GFP_KERNEL,\s*buf->attrs\)',
        'sq2_intbuf_alloc(inst, buf)', helpers)
    helpers, free_count = re.subn(
        r'dma_free_attrs\((?:dev|inst->core->dev), buf->size, buf->va, buf->da,\s*buf->attrs\)',
        'sq2_intbuf_dma_free(inst, buf)', helpers)
    if (alloc_count, free_count) != (2, 6):
        raise ValueError(f'internal DMA site counts: {alloc_count}/{free_count}')
    (args.output / 'helpers.c').write_text(helpers)
    fw = (src / 'firmware.c').read_text()
    fw = replace(fw, '#include "hfi_venus_io.h"', '#include "hfi_venus_io.h"\n#include "venus-iris1.h"')
    fw = replace(fw, '\tret = venus_load_fw(core, fwpath, &mem_phys, &mem_size);',
                 '\tif (IS_IRIS1(core)) {\n'
                 '\t\tif (!IS_V4(core))\n\t\t\treturn -EINVAL;\n'
                 '\t\tvenus_iris1_clock_config(core);\n\t}\n\n'
                 '\tret = venus_load_fw(core, fwpath, &mem_phys, &mem_size);')
    (args.output / 'firmware.c').write_text(fw)
    if args.firmware_lease:
        fw = replace(fw, 'int venus_set_hw_state(struct venus_core *core, bool resume)\n{\n\tint ret;',
                     'int venus_set_hw_state(struct venus_core *core, bool resume)\n{\n\tint ret;\n\n'
                     '\t/* Independent owner already holds hardware powered; no stock SCM state call. */\n'
                     '\tif (IS_SQ2_HFI(core))\n'
                     '\t\treturn resume && core->sq2_secure_active ? 0 : -EBUSY;')
        fw = replace(fw, '#include "venus-iris1.h"',
                     '#include "venus-iris1.h"\n#include "sq2-venus-fw.h"\n'
                     '#include "sq2-venus-platform.h"\n'
                     '#include "venus-sq2-firmware.h"')
        fw = replace(fw, '\tif (!IS_ENABLED(CONFIG_QCOM_MDT_LOADER) ||',
                     '\tif (IS_SQ2_HFI(core))\n\t\treturn venus_sq2_boot(core);\n\n'
                     '\tif (!IS_ENABLED(CONFIG_QCOM_MDT_LOADER) ||')
        fw = replace(fw, 'int venus_shutdown(struct venus_core *core)\n{\n\tint ret;',
                     'int venus_shutdown(struct venus_core *core)\n{\n\tint ret;\n\n'
                     '\tif (IS_SQ2_HFI(core))\n\t\treturn venus_sq2_shutdown(core);')
        (args.output / 'firmware.c').write_text(fw)
        for name in ('sq2-fw-transaction.h', 'sq2-venus-fw.h', 'sq2-venus-fw.c',
                     'sq2_video_fw_layout.h', 'sq2_pil_mem_query.c', 'sq2-pil-region.h',
                     'venus-sq2-firmware.h'):
            shutil.copyfile(ROOT / 'probe/video' / name, args.output / name)
        with (args.output / 'Makefile').open('a') as f:
            f.write('\nvenus-core-objs += sq2-venus-fw.o sq2_pil_mem_query.o\n'
                    'ccflags-y += -DSQ2_PIL_QUERY_LIBRARY -I$(srctree)/drivers/firmware\n')
    manifest = {'status': ('compile-only-explicit-lab-match-no-hardware-validation' if args.sq2_resources
                           else 'compile-only-no-platform-match-no-hardware-validation'),
                'sq2_transport': 'explicit inactive flag; coherent 16MiB UC; bounded IOVA; SFR view; CPU-only mailboxes; no VERSION write',
                'remaining': ['core probe/recovery/remove dispatch and runtime PM adaptation',
                              'V4L2 buffer address validation and runtime', 'Android Codec2'],
                'sq2_dma': 'exact 2300/2320/2340 SID + singleton DMA group; saved/restored 32bit mask and bus limit; UC alignment and every-page translation checked',
                'firmware_lease_dispatch_compiled': args.firmware_lease,
                'sq2_platform_probe_blocked': True,
                'independent_platform_entry_compiled': args.platform_entry,
                'platform_entry_guard': 'SQ2 lab match only when explicitly requested; sq2_platform_allow defaults false; published owner retained on removal',
                'sq2_lab_match_compiled': args.sq2_resources,
                'platform_owner': 'compiled independent core/MMIO/IRQ/power/reset/ICC owner; firmware begin/end hooks connected; platform probe/recovery/remove pending',
                'input_sha256': HASHES,
                'output_sha256': {f.name: hashlib.sha256(f.read_bytes()).hexdigest()
                                  for f in sorted(args.output.iterdir()) if f.is_file()}}
    (args.output / 'candidate.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(args.output)


if __name__ == '__main__':
    main()
