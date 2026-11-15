#!/usr/bin/env python3
"""Extend an existing SC8180X EFI preboot source with PAS12 and its checked handoff."""
import argparse
from pathlib import Path
import shutil


def change(text, old, new):
    if text.count(old) != 1:
        raise ValueError("unexpected EFI source anchor: "+old[:80])
    return text.replace(old, new)


def prepare(source, output):
    types = (source/"src/pil-types.c").read_text()
    types = change(types, "static const struct pil_type pil_types[] = {",
        '''static const struct pil_type pil_types[] = {
	{ .compatible = "qcom,sc8180x-slpi-pas", .id[PIL_MAIN].full = 12 },''')
    handoff = (source/"src/sq2-preboot-rmtfs.c").read_text()
    marker = "EFI_STATUS sq2_preboot_publish_results(void)"
    helper = '''/* Optional SLPI must publish success from EFI, never infer it in Linux. */
static EFI_STATUS spx_slpi_publish(void *dest)
{
	const char *path = "/soc@0/remoteproc@2400000";
	int src = fdt_path_offset(result_dtb, path);
	int dst = fdt_path_offset(dest, path);
	int len;
	const fdt32_t *done;
	const void *trace;
	if (src == -FDT_ERR_NOTFOUND && dst == -FDT_ERR_NOTFOUND)
		return EFI_SUCCESS;
	if (src < 0 || dst < 0 ||
	    fdt_node_check_compatible(result_dtb, src, "qcom,sc8180x-slpi-pas") ||
	    fdt_node_check_compatible(dest, dst, "qcom,sc8180x-slpi-pas"))
		return EFI_INVALID_PARAMETER;
	if (!fdt_getprop(result_dtb, src, "microsoft,el2-preboot", NULL) ||
	    !fdt_getprop(dest, dst, "microsoft,el2-preboot", NULL))
		return EFI_INVALID_PARAMETER;
	done = fdt_getprop(result_dtb, src, "microsoft,el2-preboot-complete", &len);
	if (!done || len != 4 || fdt32_to_cpu(*done) != 1)
		return EFI_NOT_READY;
	done = fdt_getprop(dest, dst, "microsoft,el2-preboot-complete", &len);
	if (!done || len != 4 || fdt32_to_cpu(*done) > 1)
		return EFI_INVALID_PARAMETER;
	trace = fdt_getprop(dest, dst, "microsoft,el2-preboot-status", &len);
	if (!trace || len != 24)
		return EFI_INVALID_PARAMETER;
	trace = fdt_getprop(result_dtb, src, "microsoft,el2-preboot-status", &len);
	if (!trace || len != 24 ||
	    fdt_setprop_inplace(dest, dst, "microsoft,el2-preboot-status", trace, len) ||
	    fdt_setprop_inplace_u32(dest, dst, "microsoft,el2-preboot-complete", 1))
		return EFI_INVALID_PARAMETER;
	return EFI_SUCCESS;
}

'''
    handoff = change(handoff, marker, helper+marker)
    handoff = change(handoff, "\tWriteBackInvalidateDataCacheRange(dest, fdt_totalsize(dest));",
        '''	{
		EFI_STATUS status = spx_slpi_publish(dest);
		if (EFI_ERROR(status)) return status;
	}
	WriteBackInvalidateDataCacheRange(dest, fdt_totalsize(dest));''')
    shutil.copytree(source, output, ignore=shutil.ignore_patterns("out", ".git"))
    (output/"src/pil-types.c").write_text(types)
    (output/"src/sq2-preboot-rmtfs.c").write_text(handoff)


if __name__ == "__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("source",type=Path)
    p.add_argument("output",type=Path)
    a=p.parse_args()
    prepare(a.source.resolve(),a.output.resolve())
