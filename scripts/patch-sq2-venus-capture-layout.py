#!/usr/bin/env python3
"""Negotiate padded linear capture via S_FMT without changing coded size/crop."""
import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def apply(directory):
    names = ("core.h", "vdec.c", "hfi_cmds.c")
    original = {name: (directory / name).read_text() for name in names}
    data = dict(original)
    def replace(name, old, new):
        if data[name].count(old) != 1:
            raise ValueError("Unknown capture layout anchor in " + name + ": " + old[:80])
        data[name] = data[name].replace(old, new)
    replace("core.h", "	u32 sq2_extra_mask, sq2_extra_size;",
            "	u32 sq2_extra_mask, sq2_extra_size;\n	u32 sq2_capture_stride, sq2_capture_scan, sq2_capture_size;")
    replace("vdec.c", '#include "venus-sq2-crop.h"',
            '#include "venus-sq2-crop.h"\n#include "venus-sq2-capture-layout.h"')
    anchor = "static int vdec_try_fmt(struct file *file, void *fh, struct v4l2_format *f)"
    helper = r"""
/* Called only after generic format normalization; orig retains requested padding. */
static int sq2_capture_try(struct venus_inst *inst, struct v4l2_format *f,
                          const struct v4l2_pix_format_mplane *orig)
{
 struct v4l2_pix_format_mplane *pix = &f->fmt.pix_mp;
 u32 stride = orig->plane_fmt[0].bytesperline, size;
 int ret;
 if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ||
     !IS_SQ2_HFI(inst->core) ||
     (stride <= pix->plane_fmt[0].bytesperline && orig->height <= inst->height))
  return 0;
 if (!inst->reconfig || inst->codec_state != VENUS_DEC_STATE_CAPTURE_SETUP ||
     orig->width != inst->width || orig->pixelformat != inst->fmt_cap->pixfmt ||
     orig->field != V4L2_FIELD_NONE)
  return -EINVAL;
 ret = sq2_capture_layout(orig->pixelformat, inst->width, inst->height,
                         stride, orig->height,
                         venus_helper_get_framesz(orig->pixelformat,
                                                  inst->width, inst->height), &size);
 if (ret)
  return ret;
 pix->height = orig->height;
 pix->plane_fmt[0].bytesperline = stride;
 pix->plane_fmt[0].sizeimage = size;
 return 1;
}

static void sq2_capture_report(struct venus_inst *inst, struct v4l2_format *f)
{
 if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE && inst->sq2_capture_stride) {
  f->fmt.pix_mp.height = inst->sq2_capture_scan;
  f->fmt.pix_mp.plane_fmt[0].bytesperline = inst->sq2_capture_stride;
  f->fmt.pix_mp.plane_fmt[0].sizeimage = inst->sq2_capture_size;
 }
}

"""
    replace("vdec.c", anchor, helper + anchor)
    start = data["vdec.c"].index(anchor)
    end = data["vdec.c"].index("static int vdec_check_src_change", start)
    old = data["vdec.c"][start:end]
    new = old.replace("\n	vdec_try_fmt_common(inst, f);", "\n	struct v4l2_pix_format_mplane orig = f->fmt.pix_mp;\n	int ret;\n\n	vdec_try_fmt_common(inst, f);\n	ret = sq2_capture_try(inst, f, &orig);").replace("	return 0;", "	return ret < 0 ? ret : 0;")
    replace("vdec.c", old, new)
    start = data["vdec.c"].index("static int vdec_g_fmt(")
    end = data["vdec.c"].index("static int vdec_s_fmt(", start)
    old = data["vdec.c"][start:end]
    replace("vdec.c", old, old.replace("	vdec_try_fmt_common(inst, f);", "	vdec_try_fmt_common(inst, f);\n	sq2_capture_report(inst, f);"))
    replace("vdec.c", "	fmt = vdec_try_fmt_common(inst, f);\n", """	fmt = vdec_try_fmt_common(inst, f);
	{
		int ret = sq2_capture_try(inst, f, &orig_pixmp);
		if (ret < 0)
			return ret;
		if (ret) {
			inst->sq2_capture_stride = pixmp->plane_fmt[0].bytesperline;
			inst->sq2_capture_scan = pixmp->height;
			inst->sq2_capture_size = pixmp->plane_fmt[0].sizeimage;
			return 0;
		}
		inst->sq2_capture_stride = 0;
		inst->sq2_capture_scan = 0;
		inst->sq2_capture_size = 0;
	}
""")
    replace("vdec.c", "	ret = venus_helper_set_format_constraints(inst);",
            """	if (inst->sq2_capture_stride) {
		struct hfi_uncompressed_plane_actual_constraints_info layout = {0};
		u32 i;
		if (inst->opb_fmt != HFI_COLOR_FORMAT_NV12 &&
		    inst->opb_fmt != HFI_COLOR_FORMAT_P010)
			return -EINVAL;
		layout.buffer_type = inst->opb_buftype;
		layout.num_planes = 2;
		for (i = 0; i < 2; i++) {
			layout.plane_format[i].stride_multiples = inst->sq2_capture_stride;
			layout.plane_format[i].max_stride = inst->sq2_capture_stride;
			layout.plane_format[i].min_plane_buffer_height_multiple =
				inst->sq2_capture_scan >> i;
			layout.plane_format[i].buffer_alignment = 256;
		}
		ret = hfi_session_set_property(inst,
			HFI_PROPERTY_PARAM_UNCOMPRESSED_PLANE_ACTUAL_CONSTRAINTS_INFO, &layout);
		if (inst->opb_buftype == HFI_BUFFER_OUTPUT2)
			inst->output2_buf_size = inst->sq2_capture_size;
		else
			inst->output_buf_size = inst->sq2_capture_size;
	} else {
		ret = venus_helper_set_format_constraints(inst);
	}""")
    replace("vdec.c", "		inst->output_buf_size = sizes[0];",
            "		sizes[0] = max(sizes[0], inst->sq2_capture_size);\n		inst->output_buf_size = sizes[0];")
    anchor = "	format.fmt.pix_mp.width = ev_data->width;"
    replace("vdec.c", anchor, "	inst->sq2_capture_stride = 0;\n	inst->sq2_capture_scan = 0;\n	inst->sq2_capture_size = 0;\n" + anchor)
    replace("vdec.c", "\tcase V4L2_SEL_TGT_COMPOSE_PADDED:\n", "")
    replace("vdec.c", "\tcase V4L2_SEL_TGT_COMPOSE_DEFAULT:",
            """\tcase V4L2_SEL_TGT_COMPOSE_PADDED:
\t\tif (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
\t\t\treturn -EINVAL;
\t\ts->r.width = inst->sq2_capture_stride ?
\t\t\tinst->sq2_capture_stride / (inst->fmt_cap->pixfmt == V4L2_PIX_FMT_P010 ? 2 : 1) : inst->width;
\t\ts->r.height = inst->sq2_capture_scan ? inst->sq2_capture_scan : inst->height;
\t\tbreak;
\tcase V4L2_SEL_TGT_COMPOSE_DEFAULT:""")
    replace("vdec.c", "\t\t\tif (bufreq.size > inst->output2_buf_size)\n\t\t\t\treturn -EINVAL;",
            """\t\t\tif (bufreq.size > inst->output2_buf_size) {
\t\t\t\tif (inst->sq2_capture_stride)
\t\t\t\t\tdev_warn_ratelimited(core->dev, "native capture firmware requires %u, allocation %u\\n",
\t\t\t\t\t\tbufreq.size, inst->output2_buf_size);
\t\t\t\treturn -EINVAL;
\t\t\t}""")
    # The HFI v4 firmware ABI supports this property; upstream only wired v6.
    # Move its unchanged wire layout into v4 so v6 reaches the same serializer.
    hfi = data["hfi_cmds.c"]
    start = hfi.index("\tcase HFI_PROPERTY_PARAM_UNCOMPRESSED_PLANE_ACTUAL_CONSTRAINTS_INFO: {")
    end = hfi.index("\tcase HFI_PROPERTY_CONFIG_HEIC_FRAME_QUALITY:", start)
    case = hfi[start:end]
    if case.count("sizeof(*info)") != 1 or case.count("info->plane_format[1]") != 1:
        raise ValueError("Unknown plane constraints serializer")
    checked = case.replace("\t\tinfo->buffer_type = in->buffer_type;",
        "\t\tif (!in->num_planes || in->num_planes > 2)\n\t\t\treturn -EINVAL;\n"
        "\t\tmemset(info, 0, sizeof(*info));\n\t\tinfo->buffer_type = in->buffer_type;")
    hfi = hfi[:start] + hfi[end:]
    start = hfi.index("pkt_session_set_property_4xx(")
    pos = hfi.index("\tswitch (ptype) {\n", start) + len("\tswitch (ptype) {\n")
    data["hfi_cmds.c"] = hfi[:pos] + checked + hfi[pos:]
    header = ROOT / "probe/video/venus-sq2-capture-layout.h"
    if (directory / header.name).exists():
        raise ValueError("Capture layout candidate already patched")
    for name in names:
        (directory / name).write_text(data[name])
    (directory / header.name).write_bytes(header.read_bytes())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    apply(parser.parse_args().directory)
