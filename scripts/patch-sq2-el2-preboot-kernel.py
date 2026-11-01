#!/usr/bin/env python3
"""Opt-in SC8180X remoteproc/RMTFS handoff from the paired EFI preboot driver.

No failed SCM call is treated as success. The EFI-created result properties
are required before Linux publishes an attached remote or shared-memory node.
"""
from pathlib import Path
import sys

k = Path(sys.argv[1])
def patch(rel, pairs):
    p = k / rel
    s = p.read_text()
    if 'SQ2_EL2_PREBOOT_HANDOFF' in s:
        return
    for old, new in pairs:
        assert s.count(old) == 1, (rel, old)
        s = s.replace(old, new)
    p.write_text(s)

attach = r'''
/* SQ2_EL2_PREBOOT_HANDOFF: only for firmware authenticated by our EFI stage. */
static int sq2_preboot_requested(struct device *dev)
{
	u32 done;
	struct device_node *np = dev->of_node;
	if (!of_property_read_bool(np, "microsoft,el2-preboot"))
		return 0;
	if (!is_kernel_in_hyp_mode() ||
	    !of_machine_is_compatible("microsoft,surface-pro-x") ||
	    !(of_device_is_compatible(np, "qcom,sc8180x-adsp-pas") ||
	      of_device_is_compatible(np, "qcom,sc8180x-cdsp-pas") ||
	      of_device_is_compatible(np, "qcom,sc8180x-mpss-pas")))
		return -EINVAL;
	if (of_property_read_u32(np, "microsoft,el2-preboot-complete", &done) || done != 1)
		return dev_err_probe(dev, -EIO, "EFI preboot not completed; refusing firmware reload\n");
	return 1;
}

static int sq2_preboot_attach(struct rproc *rproc)
{
	struct qcom_adsp *adsp = rproc->priv;
	int ret = sq2_preboot_requested(adsp->dev);
	if (ret != 1)
		return ret < 0 ? ret : -EINVAL;
	/* No proxy resources were acquired here. Ignore stale handover callbacks
	 * that would otherwise release unowned clock/regulator/domain votes. */
	ret = qcom_q6v5_prepare(&adsp->q6v5);
	if (!ret)
		dev_info(adsp->dev, "SQ2 EL2: attached to EFI-started PAS %d\n", adsp->pas_id);
	return ret;
}

static int sq2_preboot_no_reset(struct rproc *rproc)
{
	return -EOPNOTSUPP;
}

static const struct rproc_ops sq2_preboot_ops = {
	.attach = sq2_preboot_attach,
	.start = sq2_preboot_no_reset,
	.stop = sq2_preboot_no_reset,
	.detach = sq2_preboot_no_reset,
};

'''
patch('drivers/remoteproc/qcom_q6v5_pas.c', [
    ('#include "qcom_common.h"', '#include <asm/virt.h>\n\n#include "qcom_common.h"'),
    ('\tbool decrypt_shutdown;\n\tconst char *info_name;', '\tbool decrypt_shutdown;\n\tbool sq2_preboot;\n\tconst char *info_name;'),
    ('static const struct rproc_ops adsp_ops = {', attach + 'static const struct rproc_ops adsp_ops = {'),
    ('\tconst struct rproc_ops *ops = &adsp_ops;\n\tint ret;', '\tconst struct rproc_ops *ops = &adsp_ops;\n\tint ret, preboot;'),
    ('\tif (desc->minidump_id)\n\t\tops = &adsp_minidump_ops;',
     '\tif (desc->minidump_id)\n\t\tops = &adsp_minidump_ops;\n\tpreboot = sq2_preboot_requested(&pdev->dev);\n\tif (preboot < 0)\n\t\treturn preboot;\n\tif (preboot)\n\t\tops = &sq2_preboot_ops;'),
    ('\trproc->auto_boot = desc->auto_boot;', '\trproc->auto_boot = desc->auto_boot;\n\tif (preboot) {\n\t\trproc->state = RPROC_DETACHED;\n\t\trproc->auto_boot = true;\n\t\trproc->recovery_disabled = true;\n\t\trproc->sysfs_read_only = true;\n\t}'),
    ('\tadsp->dev = &pdev->dev;', '\tadsp->dev = &pdev->dev;\n\tadsp->sq2_preboot = preboot;'),
    ('\tret = adsp_alloc_memory_region(adsp);', '\t/* Preboot memory is already assigned to the DSP: do not map or reload it. */\n\tret = preboot ? 0 : adsp_alloc_memory_region(adsp);'),
    ('\tret = adsp_assign_memory_region(adsp);', '\tret = preboot ? 0 : adsp_assign_memory_region(adsp);'),
    ('\t\t\t     qcom_pas_handover);', '\t\t\t     preboot ? NULL : qcom_pas_handover);'),
])

rmtfs = r'''
	/* SQ2_EL2_PREBOOT_HANDOFF: preserve the actual EL1 assignment. */
	if (of_property_read_bool(node, "microsoft,el2-preboot-rmtfs")) {
		u32 done, id, owner;
		if (!is_kernel_in_hyp_mode() ||
		    !of_machine_is_compatible("microsoft,surface-pro-x") ||
		    rmem->base != 0x85500000 || rmem->size != 0x200000 ||
		    of_property_read_u32(node, "qcom,client-id", &id) || id != 1 ||
		    of_property_count_u32_elems(node, "qcom,vmid") != 1 ||
		    of_property_read_u32(node, "qcom,vmid", &owner) || owner != 15 ||
		    of_property_read_u32(node, "microsoft,el2-preboot-complete", &done) || done != 1)
			return dev_err_probe(&pdev->dev, -EIO, "EFI RMTFS assignment not verified\n");
		preboot = true;
	}
'''
patch('drivers/soc/qcom/rmtfs_mem.c', [
    ('#include <linux/kernel.h>', '#include <asm/virt.h>\n#include <linux/kernel.h>'),
    ('\tint ret, i;\n', '\tint ret, i;\n\tbool preboot = false;\n'),
    ('\tret = of_property_read_u32(node, "qcom,client-id", &client_id);', rmtfs + '\n\tret = of_property_read_u32(node, "qcom,client-id", &client_id);'),
    ('\tnum_vmids = of_property_count_u32_elems(node, "qcom,vmid");',
     '\tif (preboot) {\n\t\t/* perms stays zero: EL2 must not attempt an EL1 ownership teardown. */\n\t\tdev_info(&pdev->dev, "SQ2 EL2: using EFI-assigned RMTFS memory\\n");\n\t\tdev_set_drvdata(&pdev->dev, rmtfs_mem);\n\t\treturn 0;\n\t}\n\n\tnum_vmids = of_property_count_u32_elems(node, "qcom,vmid");'),
])
print('Prepared opt-in EFI handoff; compile and device validation required')

# pd-mapper locates service maps relative to remoteproc/firmware. Generic
# externally started firmware remains unknown; this paired EFI path knows it.
known_firmware = r'''
/* SQ2_EL2_PREBOOT_HANDOFF: only expose the verified paired EFI image name. */
static bool sq2_preboot_firmware_known(struct rproc *rproc)
{
	struct device_node *np = rproc->dev.parent ? rproc->dev.parent->of_node : NULL;
	const char *name;
	u32 done;
	return np && is_kernel_in_hyp_mode() &&
		of_machine_is_compatible("microsoft,surface-pro-x") &&
		(of_device_is_compatible(np, "qcom,sc8180x-adsp-pas") ||
		 of_device_is_compatible(np, "qcom,sc8180x-cdsp-pas") ||
		 of_device_is_compatible(np, "qcom,sc8180x-mpss-pas")) &&
		of_property_read_bool(np, "microsoft,el2-preboot") &&
		!of_property_read_u32(np, "microsoft,el2-preboot-complete", &done) && done == 1 &&
		!of_property_read_string(np, "firmware-name", &name) &&
		!strcmp(name, rproc->firmware);
}

'''
patch('drivers/remoteproc/remoteproc_sysfs.c', [
    ('#include <linux/remoteproc.h>', '#include <linux/remoteproc.h>\n#include <linux/of.h>\n#include <asm/virt.h>'),
    ('/* Expose the loaded / running firmware name via sysfs */', known_firmware + '/* Expose the loaded / running firmware name via sysfs */'),
    ('if (rproc->state == RPROC_ATTACHED)\n\t\tfirmware = "unknown";',
     'if (rproc->state == RPROC_ATTACHED && !sq2_preboot_firmware_known(rproc))\n\t\tfirmware = "unknown";'),
])

# Linux 6.3 copies the EFI DT before ExitBootServices, while qebspil starts
# firmware in the late EBS callback. Copy only the four verified result cells
# afterwards. Do not republish/reload firmware or use EFI services after EBS.
sync_results = r'''
/* SQ2_EL2_PREBOOT_HANDOFF: late EBS results belong in the kernel's DT copy. */
static void sq2_preboot_copy_results(const void *source, void *dest)
{
	static const char paths[][48] = {
		"/soc@0/remoteproc@17300000",
		"/soc@0/remoteproc@8300000",
		"/soc@0/remoteproc@4080000",
		"/reserved-memory/memory@85500000",
	};
	unsigned int i;
	if (!source || fdt_check_header(source) ||
	    fdt_node_check_compatible(source, 0, "microsoft,surface-pro-x") ||
	    fdt_node_check_compatible(dest, 0, "microsoft,surface-pro-x"))
		return;
	for (i = 0; i < ARRAY_SIZE(paths); i++) {
		const char *opt = i == 3 ? "microsoft,el2-preboot-rmtfs" :
			"microsoft,el2-preboot";
		const char *result = "microsoft,el2-preboot-complete";
		const fdt32_t *done, *pending;
		const void *trace;
		int s = fdt_path_offset(source, paths[i]);
		int d = fdt_path_offset(dest, paths[i]);
		int slen, dlen;
		if (s < 0 || d < 0 || !fdt_getprop(source, s, opt, NULL) ||
		    !fdt_getprop(dest, d, opt, NULL))
			continue;
		done = fdt_getprop(source, s, result, &slen);
		pending = fdt_getprop(dest, d, result, &dlen);
		if (done && pending && slen == sizeof(*done) &&
		    dlen == sizeof(*pending) && fdt32_to_cpu(*done) == 1 &&
		    fdt32_to_cpu(*pending) == 0)
			fdt_setprop_inplace_u32(dest, d, result, 1);
		trace = fdt_getprop(source, s, "microsoft,el2-preboot-status", &slen);
		if (trace && slen == 24)
			fdt_setprop_inplace(dest, d, "microsoft,el2-preboot-status", trace, slen);
	}
}

'''
patch('drivers/firmware/efi/libstub/fdt.c', [
    ('static void fdt_update_cell_size(void *fdt)',
     sync_results + 'static void fdt_update_cell_size(void *fdt)'),
    ('\t\tif (efi_novamap)\n',
     '\t\tsq2_preboot_copy_results((void *)fdt_addr, (void *)*new_fdt_addr);\n\n\t\tif (efi_novamap)\n'),
])

# Diagnostic opt-in only. Capture the already-populated ARM64 screen_info
# before EBS; the post-EBS markers perform raw framebuffer writes, not EFI IO.
# Reuse the bounded, host-tested glyph renderer from the isolated launcher.
root = Path(__file__).resolve().parents[1]
renderer = (root / 'probe/virtualization/el2-progress-fb.h').read_text()
renderer = renderer[renderer.index('static void sq2_progress_mark'):]
for old, new in [('UINT64', 'u64'), ('UINT32', 'u32'), ('UINT8', 'u8'),
                 ('sq2_progress_', 'sq2_stub_progress_')]:
    renderer = renderer.replace(old, new)
header = r'''/* SPDX-License-Identifier: BSD-3-Clause */
/* SQ2_EL2_STUB_PROGRESS: explicit diagnostic only, no EFI calls after EBS. */
#include <linux/screen_info.h>
static volatile u32 *sq2_stub_progress_fb;
static u32 sq2_stub_progress_stride;
static void sq2_stub_progress_init(const void *fdt, const char *cmdline)
{
	struct screen_info *si;
	u64 base;
	if (!IS_ENABLED(CONFIG_ARM64) || !fdt ||
	    fdt_node_check_compatible(fdt, 0, "microsoft,surface-pro-x") ||
	    !strstr(cmdline, "sq2.el2_trace=1"))
		return;
	/* ARM64 returns its existing screen_info; no new allocation occurs. */
	si = alloc_screen_info();
	if (!si || si->orig_video_isVGA != VIDEO_TYPE_EFI || si->lfb_depth != 32 ||
	    si->lfb_width < 512 || si->lfb_height < 48 ||
	    si->lfb_linelength < 512 * 4 || si->lfb_linelength % 4)
		return;
	base = si->lfb_base;
	if (si->capabilities & VIDEO_CAPABILITY_64BIT_BASE)
		base |= (u64)si->ext_lfb_base << 32;
	if (!base) return;
	sq2_stub_progress_fb = (void *)(unsigned long)base;
	sq2_stub_progress_stride = si->lfb_linelength / 4;
}
''' + renderer
header_path = k / 'drivers/firmware/efi/libstub/sq2-el2-progress.h'
if not header_path.exists() or header_path.read_text() != header:
    header_path.write_text(header)
fdt = k / 'drivers/firmware/efi/libstub/fdt.c'
text = fdt.read_text()
if 'SQ2_EL2_STUB_PROGRESS' not in text:
    replacements = [
        ('#include "efistub.h"', '#include "efistub.h"\n/* SQ2_EL2_STUB_PROGRESS */\n#include "sq2-el2-progress.h"'),
        ('\tstatus = efi_exit_boot_services(handle, &priv, exit_boot_func);',
         '\tsq2_stub_progress_init((void *)fdt_addr, cmdline_ptr);\n'
         '\tstatus = efi_exit_boot_services(handle, &priv, exit_boot_func);\n'
         '\tsq2_stub_progress_mark(0x40, status);'),
        ('\t\tsq2_preboot_copy_results((void *)fdt_addr, (void *)*new_fdt_addr);',
         '\t\tsq2_stub_progress_mark(0x41, fdt_addr);\n'
         '\t\tsq2_preboot_copy_results((void *)fdt_addr, (void *)*new_fdt_addr);\n'
         '\t\tsq2_stub_progress_mark(0x42, *new_fdt_addr);'),
        ('\tefi_enter_kernel(kernel_addr, fdt_addr, fdt_totalsize((void *)fdt_addr));',
         '\tsq2_stub_progress_mark(0x50, kernel_addr);\n'
         '\tefi_enter_kernel(kernel_addr, fdt_addr, fdt_totalsize((void *)fdt_addr));'),
    ]
    for old, new in replacements:
        assert text.count(old) == 1, old
        text = text.replace(old, new)
    fdt.write_text(text)

# Diagnostic modem proxy votes, opt-in on the modem DT node only. These are
# the existing driver's resource requests, with no firmware reload or reset.
pas = k / 'drivers/remoteproc/qcom_q6v5_pas.c'
text = pas.read_text()
if 'SQ2_EL2_MODEM_PROXY_TEST' not in text:
    proxy_test = r'''
/* SQ2_EL2_MODEM_PROXY_TEST: bounded by the RAM diagnostic's reboot. */
static int sq2_preboot_modem_proxy(struct qcom_adsp *adsp)
{
	int ret;
	size_t size;
	const char *reason;
	if (adsp->pas_id != 4 ||
	    !of_property_read_bool(adsp->dev->of_node, "microsoft,el2-modem-proxy-test"))
		return 0;
	reason = qcom_smem_get(QCOM_SMEM_HOST_ANY, 421, &size);
	if (!IS_ERR(reason) && size)
		dev_info(adsp->dev, "SQ2 modem SMEM421 before votes: %.*s\n",
			 (int)min_t(size_t, size, 256), reason);
	ret = adsp_pds_enable(adsp, adsp->proxy_pds, adsp->proxy_pd_count);
	if (ret < 0) return ret;
	ret = clk_prepare_enable(adsp->xo);
	if (ret) goto disable_pds;
	ret = clk_prepare_enable(adsp->aggre2_clk);
	if (ret) goto disable_xo;
	if (adsp->cx_supply) {
		ret = regulator_enable(adsp->cx_supply);
		if (ret) goto disable_aggre;
	}
	if (adsp->px_supply) {
		ret = regulator_enable(adsp->px_supply);
		if (ret) goto disable_cx;
	}
	dev_info(adsp->dev, "SQ2 modem proxy test: existing start votes held until diagnostic reboot\n");
	return 0;
disable_cx:
	if (adsp->cx_supply) regulator_disable(adsp->cx_supply);
disable_aggre:
	clk_disable_unprepare(adsp->aggre2_clk);
disable_xo:
	clk_disable_unprepare(adsp->xo);
disable_pds:
	adsp_pds_disable(adsp, adsp->proxy_pds, adsp->proxy_pd_count);
	return ret;
}

'''
    anchor = 'static int sq2_preboot_attach(struct rproc *rproc)'
    assert text.count(anchor) == 1
    text = text.replace(anchor, proxy_test + anchor)
    anchor = '\tret = qcom_q6v5_prepare(&adsp->q6v5);\n\tif (!ret)\n\t\tdev_info(adsp->dev, "SQ2 EL2: attached'
    replacement = ('\tret = qcom_q6v5_prepare(&adsp->q6v5);\n'
                   '\tif (!ret) {\n\t\tret = sq2_preboot_modem_proxy(adsp);\n'
                   '\t\tif (ret) qcom_q6v5_unprepare(&adsp->q6v5);\n\t}\n'
                   '\tif (!ret)\n\t\tdev_info(adsp->dev, "SQ2 EL2: attached')
    assert text.count(anchor) == 1
    pas.write_text(text.replace(anchor, replacement))

# Upgrade both fresh trees and the existing v7 build, using exact anchors.
# EBS callbacks run before EL2; give qebspil the final destination in advance.
text = fdt.read_text()
if 'SQ2_EL2_PREBOOT_DESTINATION' not in text:
    destination = r'''
/* SQ2_EL2_PREBOOT_HANDOFF / SQ2_EL2_PREBOOT_DESTINATION */
static efi_status_t sq2_preboot_set_destination(void *source, void *dest)
{
	const fdt64_t *property;
	fdt64_t address = cpu_to_fdt64((unsigned long)dest);
	int len;
	if (!source || fdt_check_header(source))
		return EFI_SUCCESS;
	property = fdt_getprop(source, 0, "microsoft,el2-kernel-fdt", &len);
	if (!property)
		return EFI_SUCCESS;
	if (len != sizeof(address) ||
	    fdt_node_check_compatible(source, 0, "microsoft,surface-pro-x") ||
	    fdt_node_check_compatible(dest, 0, "microsoft,surface-pro-x") ||
	    fdt_setprop_inplace(source, 0, "microsoft,el2-kernel-fdt", &address, sizeof(address)))
		return EFI_LOAD_ERROR;
	return EFI_SUCCESS;
}

'''
    replacements = [
        (sync_results, destination),
        ('\tpriv.new_fdt_addr = (void *)*new_fdt_addr;',
         '\tpriv.new_fdt_addr = (void *)*new_fdt_addr;\n'
         '\tstatus = sq2_preboot_set_destination((void *)fdt_addr, priv.new_fdt_addr);\n'
         '\tif (status != EFI_SUCCESS) {\n'
         '\t\tefi_err("SQ2 preboot DT destination rejected.\\n");\n'
         '\t\tgoto fail_free_new_fdt;\n\t}'),
        ('\t\tsq2_stub_progress_mark(0x41, fdt_addr);\n'
         '\t\tsq2_preboot_copy_results((void *)fdt_addr, (void *)*new_fdt_addr);\n', ''),
    ]
    for old, new in replacements:
        assert text.count(old) == 1, old
        text = text.replace(old, new)
    fdt.write_text(text)
