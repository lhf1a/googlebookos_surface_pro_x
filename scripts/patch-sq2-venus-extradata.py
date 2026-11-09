#!/usr/bin/env python3
'''Add opt-in firmware HDR extradata plane to the paired SQ2 Venus source.
All anchors are checked before writing. Mask=0 retains the one-plane ABI.
This does not advertise Android HDR or deploy a module.
'''
import argparse
from pathlib import Path

DPB_EXTRA_SIZE = '''		buf->size = dpb_size;
		if (inst->sq2_extra_mask) {
			u32 total;
			ret = sq2_extra_dpb_size(dpb_size, inst->sq2_extra_size, PAGE_SIZE, &total);
			if (ret)
				goto fail;
			buf->size = total;
		}'''
OLD_DPB_EXTRA_SIZE = DPB_EXTRA_SIZE.replace(
    "\t\t\tif (ret)\n\t\t\t\tgoto fail;",
    "\t\t\tif (ret) {\n\t\t\t\tkfree(buf);\n\t\t\t\tgoto fail;\n\t\t\t}")

def fix_dpb_cleanup(text):
    """Leave the failed allocation to its existing common cleanup label."""
    old, new = text.count(OLD_DPB_EXTRA_SIZE), text.count(DPB_EXTRA_SIZE)
    if old == 1 and not new:
        return text.replace(OLD_DPB_EXTRA_SIZE, DPB_EXTRA_SIZE, 1)
    if new == 1 and not old:
        return text
    raise ValueError("Unknown DPB extradata allocation cleanup anchor")


def patch_buffer_requirements(text):
    old = """	req_bytes = pkt->shdr.hdr.size - sizeof(*pkt);

	if (!req_bytes || req_bytes % sizeof(*buf_req) || !pkt->data[0])"""
    new = """	if (pkt->shdr.hdr.size < sizeof(*pkt))
		return HFI_ERR_SESSION_INVALID_PARAMETER;
	req_bytes = pkt->shdr.hdr.size - sizeof(*pkt);

	if (!req_bytes || req_bytes % sizeof(*buf_req) ||
	    req_bytes / sizeof(*buf_req) > HFI_BUFFER_TYPE_MAX || !pkt->data[0])"""
    if text.count(old) != 1:
        raise ValueError('Unknown HFI requirements length anchor')
    return text.replace(old, new, 1)

def apply(directory, root):
    edits = {}
    def replace(name, old, new, count=1):
        text = edits.get(name, (directory/name).read_text())
        if text.count(old) != count:
            raise ValueError(f"Unknown extradata anchor in {name}: {old!r}")
        edits[name] = text.replace(old, new)
    replace('core.h', '\tbool sq2_end_sent, sq2_end_confirmed;',
            '\tbool sq2_end_sent, sq2_end_confirmed;\n\tu32 sq2_extra_mask, sq2_extra_size;')
    replace('vdec_ctrls.c', 'static int vdec_op_s_ctrl(',
            '#include "venus-sq2-extradata.h"\n\nstatic int vdec_op_s_ctrl(')
    anchor = '\tswitch (ctrl->id) {'
    if edits['vdec_ctrls.c'].count(anchor) != 2:
        raise ValueError('Unknown control setter')
    edits['vdec_ctrls.c'] = edits['vdec_ctrls.c'].replace(anchor, '''	switch (ctrl->id) {
	case SQ2_CID_HDR_EXTRADATA:
		if (!IS_SQ2_HFI(inst->core) || inst->state != INST_UNINIT)
			return -EBUSY;
		inst->sq2_extra_mask = ctrl->val;
		inst->sq2_extra_size = 0;
		break;''', 1)
    replace('vdec_ctrls.c', '\tret = v4l2_ctrl_handler_init(&inst->ctrl_handler, 12);',
            '\tret = v4l2_ctrl_handler_init(&inst->ctrl_handler, 13);')
    replace('vdec_ctrls.c', '\tctrl = v4l2_ctrl_new_std_menu(&inst->ctrl_handler, &vdec_ctrl_ops,\n\t\tV4L2_CID_MPEG_VIDEO_MPEG4_PROFILE,',
'''	if (IS_SQ2_HFI(inst->core)) {
		const struct v4l2_ctrl_config extra = {
			.ops = &vdec_ctrl_ops, .id = SQ2_CID_HDR_EXTRADATA,
			.name = "SQ2 firmware HDR extradata",
			.type = V4L2_CTRL_TYPE_BITMASK, .min = 0,
			.max = SQ2_EXTRA_MASK, .def = 0,
		};
		v4l2_ctrl_new_custom(&inst->ctrl_handler, &extra, NULL);
	}
	ctrl = v4l2_ctrl_new_std_menu(&inst->ctrl_handler, &vdec_ctrl_ops,
		V4L2_CID_MPEG_VIDEO_MPEG4_PROFILE,''')
    replace('hfi_cmds.c', '#include "hfi_cmds.h"',
            '#include "hfi_cmds.h"\n#include "venus-sq2-extradata.h"')
    replace('hfi_cmds.c', '\tswitch (ptype) {\n\tcase HFI_PROPERTY_CONFIG_FRAME_RATE: {',
'''	switch (ptype) {
	case SQ2_HFI_EXTRA_VUI:
	case SQ2_HFI_EXTRA_MASTERING:
	case SQ2_HFI_EXTRA_LIGHT: {
		struct sq2_extra_config *in = pdata, *config = prop_data;
		if (in->enable > 1 || in->index != ptype)
			return -EINVAL;
		*config = *in;
		pkt->shdr.hdr.size += sizeof(u32) + sizeof(*config);
		break;
	}
	case HFI_PROPERTY_CONFIG_FRAME_RATE: {''')
    replace('vdec.c', '#include "venus-sq2-omx.h"',
            '#include "venus-sq2-omx.h"\n#define SQ2_EXTRADATA_DECODER\n#include "venus-sq2-extradata.h"')
    replace('vdec.c', '\tpixmp->num_planes = fmt->num_planes;',
'''	pixmp->num_planes = fmt->num_planes;
	if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE && inst->sq2_extra_mask) {
		pixmp->num_planes = 2;
		memset(&pfmt[1], 0, sizeof(pfmt[1]));
		pfmt[1].sizeimage = inst->sq2_extra_size;
	}''')
    replace('vdec.c', '\t\tret = vdec_check_src_change(inst);\n\t\tif (ret)\n\t\t\treturn ret;',
'''		ret = vdec_check_src_change(inst);
		if (ret)
			return ret;
		if (inst->sq2_extra_mask && inst->state >= INST_INIT) {
			ret = sq2_extra_query(inst);
			if (ret)
				return ret;
		}''')
    replace('vdec.c', '\tsq2_vdec_trace(inst, "input_resolution_end", ret);\n\tif (ret)\n\t\tgoto deinit;',
'''	sq2_vdec_trace(inst, "input_resolution_end", ret);
	if (ret)
		goto deinit;
	ret = sq2_extra_enable(inst);
	if (ret)
		goto deinit;''')
    replace('vdec.c', '\t*out_num = HFI_BUFREQ_COUNT_MIN(&bufreq, ver);\n\n\treturn 0;',
            '\t*out_num = HFI_BUFREQ_COUNT_MIN(&bufreq, ver);\n\n\treturn sq2_extra_query(inst);')
    replace('vdec.c', '\t\t    *num_planes != inst->fmt_cap->num_planes)',
            '\t\t    *num_planes != (inst->sq2_extra_mask ? 2 : inst->fmt_cap->num_planes))')
    replace('vdec.c', '\t\t    sizes[0] < output_buf_size)\n\t\t\treturn -EINVAL;',
'''		    sizes[0] < output_buf_size)
			return -EINVAL;
		if (q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE && inst->sq2_extra_mask &&
		    (!inst->sq2_extra_size || sizes[1] < inst->sq2_extra_size))
			return -EINVAL;''')
    replace('vdec.c', '\t\t*num_planes = inst->fmt_cap->num_planes;',
'''		*num_planes = inst->sq2_extra_mask ? 2 : inst->fmt_cap->num_planes;
		if (inst->sq2_extra_mask)
			sizes[1] = inst->sq2_extra_size;''')
    replace('vdec.c', '\t\tvb2_set_plane_payload(vb, 0, bytesused);',
'''		vb2_set_plane_payload(vb, 0, bytesused);
		if (inst->sq2_extra_mask && vb->num_planes == 2)
			vb2_set_plane_payload(vb, 1,
				(hfi_flags & HFI_BUFFERFLAG_EXTRADATA) ? inst->sq2_extra_size : 0);''')
    replace('helpers.c', 'static void free_dpb_buf(',
            '#define SQ2_EXTRADATA_BUFFERS\n#include "venus-sq2-extradata.h"\n\nstatic void free_dpb_buf(')
    before = '''	if (IS_SQ2_HFI(inst->core) && (vb->num_planes != 1 ||
	    !sq2_venus_buffer_valid(vb2_dma_contig_plane_dma_addr(vb, 0),
	                            vb2_plane_size(vb, 0))))
		return -ERANGE;'''
    replace('helpers.c', before, '''	if (IS_SQ2_HFI(inst->core)) {
		int ret = sq2_extra_validate(inst, vb);
		if (ret)
			return ret;
	}''', count=2)
    replace('helpers.c', '''		ret = sq2_venus_scope_bind(inst, vb->planes[0].mem_priv);
		if (ret) return ret;
	}
	ret = hfi_session_process_buf(inst, &fdata);''',
'''		ret = sq2_venus_scope_bind(inst, vb->planes[0].mem_priv);
		if (ret) return ret;
		if (inst->session_type == VIDC_SESSION_TYPE_DEC &&
		    type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE && inst->sq2_extra_mask) {
			ret = sq2_extra_validate(inst, vb);
			if (ret) return ret;
			ret = sq2_venus_scope_bind(inst, vb->planes[1].mem_priv);
			if (ret) return ret;
			fdata.extradata_addr = vb2_dma_contig_plane_dma_addr(vb, 1);
			fdata.extradata_size = inst->sq2_extra_size;
		}
	}
	ret = hfi_session_process_buf(inst, &fdata);''')
    replace('helpers.c', '\t\tbuf->size = dpb_size;', DPB_EXTRA_SIZE)
    replace('helpers.c', '\t\tif (dpb_size > buf->size) {',
'''		if (dpb_size > buf->size || (inst->sq2_extra_mask &&
		    inst->sq2_extra_size > buf->size - dpb_size)) {''')
    replace('helpers.c', '\t\tfdata.clnt_data = buf->dpb_out_tag;',
'''		fdata.clnt_data = buf->dpb_out_tag;
		if (inst->sq2_extra_mask) {
			/* Downstream also supplies extradata for the internal DPB stream. */
			if (!sq2_venus_buffer_valid(buf->da, buf->size))
				return -ERANGE;
			fdata.alloc_len = dpb_size;
			fdata.extradata_addr = buf->da + dpb_size;
			fdata.extradata_size = inst->sq2_extra_size;
		}''')
    edits['hfi_msgs.c'] = patch_buffer_requirements((directory/'hfi_msgs.c').read_text())
    edits['venus-sq2-extradata.h'] = (root/'probe/video/venus-sq2-extradata.h').read_text()
    for name, text in edits.items():
        (directory/name).write_text(text)

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory', type=Path)
    p.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    p.add_argument('--dpb-cleanup-only', action='store_true',
                   help='Fix the duplicate free in an already-patched helpers.c')
    args = p.parse_args()
    if args.dpb_cleanup_only:
        helper = args.directory/'helpers.c'
        helper.write_text(fix_dpb_cleanup(helper.read_text()))
    else:
        apply(args.directory, args.root)
