#!/usr/bin/env python3
"""Restore hardware-filtered UBWC modifiers in an explicit QTI display source candidate."""
import argparse
from pathlib import Path

OLD = """static const uint64_t supported_format_modifiers[] = {
\t/* SQ2_NO_UBWC_MODIFIER: userspace (minigbm/mapper) only consumes linear. */
\tDRM_FORMAT_MOD_LINEAR,
\tDRM_FORMAT_MOD_INVALID
};"""
NEW = """static const uint64_t supported_format_modifiers[] = {
\tDRM_FORMAT_MOD_QCOM_COMPRESSED,
\tDRM_FORMAT_MOD_LINEAR,
\tDRM_FORMAT_MOD_INVALID
};"""
HARDWARE_FILTER = """if (modifier == DRM_FORMAT_MOD_QCOM_COMPRESSED && !has_no_ubwc)
\t\treturn dpu_find_format(format, qcom_compressed_supported_formats,
\t\t\t\tARRAY_SIZE(qcom_compressed_supported_formats));"""

def patch(text):
    if text.count(HARDWARE_FILTER) != 1:
        raise ValueError("Missing original hardware/format UBWC filter")
    if text.count(NEW) == 1 and OLD not in text:
        return text
    if text.count(OLD) != 1 or NEW in text:
        raise ValueError("Unexpected DPU modifier list; refusing an ambiguous patch")
    return text.replace(OLD, NEW)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dpu_plane",type=Path)
    args=parser.parse_args()
    if args.dpu_plane.is_symlink():
        parser.error("Pass the regular source file explicitly")
    original=args.dpu_plane.read_text()
    try: updated=patch(original)
    except ValueError as error:parser.error(str(error))
    if updated != original:args.dpu_plane.write_text(updated)
    print("DPU_QTI_MODIFIERS_SOURCE_READY deployed=0 physical_scanout_verified=0")

if __name__ == "__main__":main()
