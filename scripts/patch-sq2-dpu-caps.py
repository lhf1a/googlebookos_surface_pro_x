#!/usr/bin/env python3
"""Add immutable catalog-backed DPU capability blobs to an explicit source directory."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE_HASHES = {
    "dpu_plane.c": "09870f2c38e669f4a69b68bc10d1d95c540eb6ea84d2f17fdd71dd12f1a033bc",
    "dpu_crtc.c": "a42232956f655009c58c5e764d553f6d61fc6be8f31e3503018f7aa51b54ac0c",
    "dpu_kms.h": "f3e2e446ad1e7b91fe868b2a3bfb2a41e33543fa711c5142d89ad6e8917cba7d",
    "dpu_kms.c": "34daaac23ebebb0baa473959bde7ac0fff08201df03ec29212df01692aa906ad",
}

def sha(data):
    return hashlib.sha256(data).hexdigest()

def one(text, old, new):
    if text.count(old) != 1:
        raise ValueError("Source anchor is missing or ambiguous: " + old[:80])
    return text.replace(old, new)

def transform(name, text):
    if name == "dpu_kms.h":
        return one(text, "\tconst struct dpu_mdss_cfg *catalog;\n",
                   "\tconst struct dpu_mdss_cfg *catalog;\n\tu32 core_rev;\n")
    if name == "dpu_kms.c":
        return one(text, "\tcore_rev = readl_relaxed(dpu_kms->mmio + 0x0);\n",
                   "\tcore_rev = readl_relaxed(dpu_kms->mmio + 0x0);\n\tdpu_kms->core_rev = core_rev;\n")
    text = one(text, '#include "dpu_kms.h"\n',
               '#include "dpu_kms.h"\n#include "dpu_caps.h"\n')
    if name == "dpu_plane.c":
        text = one(text, '\tdrm_plane_enable_fb_damage_clips(plane);\n',
                   '\tdrm_plane_enable_fb_damage_clips(plane);\n\n'
                   '\tret = dpu_caps_add_plane(plane, kms,\n'
                   '\t\tpipe == SSPP_NONE ? NULL : dpu_rm_get_sspp(&kms->rm, pipe),\n'
                   '\t\tMAX_UPSCALE_RATIO, MAX_DOWNSCALE_RATIO,\n'
                   '\t\tFRAC_16_16(1, MAX_UPSCALE_RATIO), MAX_DOWNSCALE_RATIO << 16);\n'
                   '\tif (ret)\n\t\treturn ERR_PTR(ret);\n')
    else:
        text = one(text, '\tret = drm_self_refresh_helper_init(crtc);\n',
                   '\tret = dpu_caps_add_crtc(crtc, dpu_kms);\n'
                   '\tif (ret)\n\t\treturn ERR_PTR(ret);\n\n'
                   '\tret = drm_self_refresh_helper_init(crtc);\n')
    return text

def patch(directory):
    if directory.is_symlink():
        raise ValueError("Source directory must not be a symlink")
    replacements = {}
    for name, expected in SOURCE_HASHES.items():
        path = directory / name
        if path.is_symlink():
            raise ValueError("Source file must not be a symlink: " + name)
        original = path.read_bytes()
        if sha(original) != expected:
            raise ValueError("Unknown source: " + name)
        replacements[name] = transform(name, original.decode()).encode()
    header = directory / "dpu_caps.h"
    if header.exists() or header.is_symlink():
        raise ValueError("Refusing to overwrite existing dpu_caps.h")
    replacements["dpu_caps.h"] = (ROOT / "probe/graphics/dpu-caps.h").read_bytes()
    for name, data in replacements.items():
        (directory / name).write_bytes(data)
    return {name: sha(data) for name, data in replacements.items()}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dpu_directory", type=Path)
    args = parser.parse_args()
    print(json.dumps(patch(args.dpu_directory), indent=2))

if __name__ == "__main__":
    main()
