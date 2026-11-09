#!/usr/bin/env python3
"""Add actual BT.601/709 limited DPU state/properties to the matched Linux 6.18 source."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE_SHA256 = "f3a0a600c9d19c01b4cda9f6949276ecbfd4db0c97d6c7e37fac0a849e86bd07"

def transform(text):
    def one(old, new):
        nonlocal text
        if text.count(old) != 1:
            raise ValueError("Missing/ambiguous DPU source anchor: " + old[:70])
        text = text.replace(old, new)
    one('#include <drm/drm_blend.h>\n',
        '#include <drm/drm_blend.h>\n#include <drm/drm_color_mgmt.h>\n')
    one('#include "dpu_hw_util.h"\n', '#include "dpu_hw_util.h"\n#include "dpu-sdr-color.h"\n')
    one("const struct msm_format *fmt)\n{\n\tconst struct dpu_csc_cfg *csc_ptr;\n",
        "const struct msm_format *fmt,\n\t\t\t\t\t    const struct drm_plane_state *state)\n{\n")
    one("\tif (BIT(DPU_SSPP_CSC_10BIT) & pipe->sspp->cap->features)\n"
        "\t\tcsc_ptr = &dpu_csc10_YUV2RGB_601L;\n\telse\n"
        "\t\tcsc_ptr = &dpu_csc_YUV2RGB_601L;\n\n\treturn csc_ptr;\n",
        "\treturn dpu_sdr_color_csc(state->color_encoding, state->color_range,\n"
        "\t\t!!(BIT(DPU_SSPP_CSC_10BIT) & pipe->sspp->cap->features));\n")
    one("_dpu_plane_get_csc(pipe, format);", "_dpu_plane_get_csc(pipe, format, pdpu->base.state);")
    one("\tdrm_plane_enable_fb_damage_clips(plane);\n",
        "\tdrm_plane_enable_fb_damage_clips(plane);\n\n"
        "\t/* Publish color state only for planes whose real format list contains YUV. */\n"
        "\tfor (unsigned int i = 0; i < num_formats; ++i) {\n"
        "\t\tconst struct drm_format_info *format = drm_format_info(format_list[i]);\n"
        "\t\tif (!format || !format->is_yuv)\n\t\t\tcontinue;\n"
        "\t\tret = drm_plane_create_color_properties(plane,\n"
        "\t\t\tBIT(DRM_COLOR_YCBCR_BT601) | BIT(DRM_COLOR_YCBCR_BT709),\n"
        "\t\t\tBIT(DRM_COLOR_YCBCR_LIMITED_RANGE),\n"
        "\t\t\tDRM_COLOR_YCBCR_BT601, DRM_COLOR_YCBCR_LIMITED_RANGE);\n"
        "\t\tif (ret)\n\t\t\treturn ERR_PTR(ret);\n\t\tbreak;\n\t}\n")
    return text

def patch(directory):
    plane = directory / "dpu_plane.c"
    header = directory / "dpu-sdr-color.h"
    if directory.is_symlink() or plane.is_symlink() or header.exists() or header.is_symlink():
        raise ValueError("Source symlink/existing header refused")
    data = plane.read_bytes()
    if hashlib.sha256(data).hexdigest() != SOURCE_SHA256:
        raise ValueError("Unknown DPU source")
    changed = transform(data.decode()).encode()
    payload = (ROOT / "probe/graphics/dpu-sdr-color.h").read_bytes()
    plane.write_bytes(changed);header.write_bytes(payload)
    return {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (plane, header)}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    print(json.dumps(patch(args.directory), indent=2))

if __name__ == "__main__":
    main()
