#!/usr/bin/env python3
"""Prepare an isolated qebspil candidate; no device access or deployment.

The three SC8180X DSP PAS identities already used by this kernel are added.
--venus-firmware also reproduces the deployed PAS9 metadata-only handoff and
DSP result trace; it does not authenticate or start video firmware in EFI.
The DT must explicitly opt in and reserve a zero-valued result property.
Linux takeover and RMTFS ownership are separate requirements, not implemented
by merely compiling this EFI driver.
"""
import argparse
from pathlib import Path
import io
import tarfile
import subprocess
import hashlib
import struct

ROOT = Path(__file__).resolve().parents[1]
PIN = '8e4d9e676a3b3afe136cda9b953a2139ff1a32d0'
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--venus-firmware', type=Path,
               help='enable the deployed PAS9 metadata handoff using the pinned private Venus image')
a = p.parse_args()
# Validate before creating a candidate. Generated firmware metadata stays private.
venus_metadata = None
if a.venus_firmware:
    image = a.venus_firmware.read_bytes()
    if len(image) != 1159200 or hashlib.sha256(image).hexdigest() != '4e93d34504da96320775476bbd509aa06f985b48fb781d4436c8eb812b929928':
        p.error('Venus firmware does not match the validated SQ2 image')
    phoff = struct.unpack_from('<I', image, 28)[0]
    phentsize, phnum = struct.unpack_from('<HH', image, 42)
    assert image[:7] == b'\x7fELF\x01\x01\x01' and phentsize == 32
    assert phoff + phentsize * phnum <= len(image)
    metadata = bytearray()
    for index in range(phnum):
        kind, offset, _, _, size, _, _, _ = struct.unpack_from('<8I', image, phoff + index * phentsize)
        if kind == 0 and size:
            assert offset + size <= len(image)
            metadata.extend(image[offset:offset + size])
    assert 0 < len(metadata) <= 16384
    venus_metadata = bytes(metadata)
src = ROOT / 'upstream/qebspil'
# Use the fixed source tree, including exact submodule revisions, without
# carrying Windows-only .git paths into the Linux build.
git = ['git', '-c', f'safe.directory={src}', '-c', 'core.filemode=false',
       '-c', 'core.autocrlf=true', '-C', str(src)]
head = subprocess.check_output(git + ['rev-parse', 'HEAD'], text=True).strip()
assert head == PIN, head
assert not subprocess.check_output(git + ['diff', '--ignore-submodules=all', '--name-only'], text=True).strip()
assert not a.output.exists(), a.output
def archive(command, revision, target):
    data = subprocess.check_output(command + ['archive', revision])
    target.mkdir(parents=True, exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(data)) as tar:
        tar.extractall(target, filter='data')

archive(git, PIN, a.output)
for module, revision in {
    'dtc': '2d10aa2afe35527728db30b35ec491ecb6959e5c',
    'gnu-efi': '1fee8ab566ce91b9cbab9f2c85db96566d79063b',
}.items():
    archive(['git', f'--git-dir={src / ".git/modules/external" / module}'],
            revision, a.output / 'external' / module)

def edit(name, old, new):
    path = a.output / name
    text = path.read_text()
    assert text.count(old) == 1, (name, old)
    path.write_text(text.replace(old, new))

edit('src/pil-types.c', 'static const struct pil_type pil_types[] = {', '''static const struct pil_type pil_types[] = {
	/* SC8180X qcom_q6v5_pas identities: ADSP=1, MPSS=4, CDSP=18.
	 * Proxy voting on this UEFI is not yet hardware-validated. */
	{ .compatible = "qcom,sc8180x-adsp-pas", .id[PIL_MAIN].full = 1 },
	{ .compatible = "qcom,sc8180x-mpss-pas", .id[PIL_MAIN].full = 4 },
	{ .compatible = "qcom,sc8180x-cdsp-pas", .id[PIL_MAIN].full = 18 },''')
edit('src/qebspil.h', 'struct pil {\n', 'struct pil {\n\tvoid *dtb;\n\tint dt_node;\n')
edit('src/qebspil.h', '#endif', '''extern UINT64 sq2_pas_result[4];
EFI_STATUS sq2_rmtfs_prepare(void *dtb);
EFI_STATUS sq2_rmtfs_finish(void);
EFI_STATUS sq2_preboot_publish_results(void);
EFI_STATUS sq2_scm_assign(UINT64 region, UINT64 region_size, UINT64 src,
                         UINT64 src_size, UINT64 dest, UINT64 dest_size, UINT64 *result);
EFI_STATUS sq2_preboot_note(void *dtb, int node, UINT32 stage,
                           EFI_STATUS status, const UINT64 *result);
#endif''')
edit('Makefile', '\tsrc/scm.o \\', '\tsrc/scm.o \\\n\tsrc/sq2-preboot-rmtfs.o \\')
(a.output / 'src/sq2-preboot-rmtfs.c').write_bytes(
    (ROOT / 'probe/virtualization/sq2-preboot-rmtfs.c').read_bytes().replace(b'\r\n', b'\n'))
with (a.output / 'src/scm.c').open('a') as f:
    f.write(r'''

EFI_STATUS sq2_scm_assign(UINT64 region, UINT64 region_size, UINT64 src,
                         UINT64 src_size, UINT64 dest, UINT64 dest_size, UINT64 *result)
{
	UINT64 args[SCM_MAX_NUM_PARAMETERS] = {
		region, region_size, src, src_size, dest, dest_size, 0
	};
	EFI_STATUS status = scm->ScmSipSysCall(scm, HYP_MEM_PROTECT_ASSIGN,
		HYP_MEM_PROTECT_ASSIGN_PARAM_ID, args, result);
	Print(u"SQ2 RMTFS SCM: status=%r result=%lx/%lx/%lx\n",
	      status, result[0], result[1], result[2]);
	if (EFI_ERROR(status))
		return status;
	/* This UEFI protocol returns tz_common_rsp_t, not Linux qcom_scm_res.
	 * scm_sip_interface.h documents HYP_MEM_PROTECT_ASSIGN with this
	 * response: common_rsp.status == 1 means completed; ret == 0 means
	 * success. The first cell must NOT be treated as Linux's result[0]. */
	tz_common_rsp_t response;
	_Static_assert(sizeof(response) == 2 * sizeof(UINT64), "SCM response ABI");
	CopyMem(&response, result, sizeof(response));
	return response.common_rsp.status == 1 && response.ret == 0
		? EFI_SUCCESS : EFI_DEVICE_ERROR;
}
''')
edit('src/main.c', '\tstatus = pil_finish_all();', '''	status = sq2_rmtfs_finish();
	if (EFI_ERROR(status)) {
		Print(u"SQ2 preboot: RMTFS handoff failed: %r\\n", status);
		Print(u"Preboot failed; rebooting to original default in 5 seconds.\\n");
		BS->Stall(5 * 1000 * 1000);
		RT->ResetSystem(EfiResetCold, status, 0, NULL);
		return status;
	}
	status = pil_finish_all();''')
edit('src/main.c', '\tstatus = dtb_enumerate_rprocs(dtb);', '''	status = sq2_rmtfs_prepare(dtb);
	if (EFI_ERROR(status))
		return status;
	status = dtb_enumerate_rprocs(dtb);''')
edit('src/dtb.c', '\twhile ((node = fdt_next_node', '''	if (fdt_node_check_compatible(dtb, 0, "microsoft,surface-pro-x"))
		return EFI_UNSUPPORTED;

	while ((node = fdt_next_node''')
edit('src/dtb.c', '''		if (!QEBSPIL_ALWAYS_START && !fdt_getprop(dtb, node, "qcom,broken-reset", NULL))''', '''		if (!fdt_getprop(dtb, node, "microsoft,el2-preboot", NULL))
			continue;
		/* Never invent success: kernel attachment requires a later 0 -> 1. */
		{
			int len;
			const fdt32_t *done = fdt_getprop(dtb, node, "microsoft,el2-preboot-complete", &len);
			if (!done || len != sizeof(*done) || fdt32_to_cpu(*done))
				return EFI_INVALID_PARAMETER;
		}
		if (!QEBSPIL_ALWAYS_START && !fdt_getprop(dtb, node, "qcom,broken-reset", NULL))''')
edit('src/dtb.c', '\t\tstatus = dtb_enumerate_rproc(&ctx, node, pil);', '''		pil->dtb = (void *)dtb;
		pil->dt_node = node;
		status = dtb_enumerate_rproc(&ctx, node, pil);''')
edit('src/scm.c', 'static QCOM_SCM_PROTOCOL *scm;',
     'static QCOM_SCM_PROTOCOL *scm;\nUINT64 sq2_pas_result[4];')
# shortcut: PAS response ABI is unverified, collect raw results before enforcing it.
for command in ('TZ_PIL_INIT_ID', 'TZ_PIL_MEM_ID',
                'TZ_PIL_AUTH_RESET_ID', 'TZ_PIL_UNLOCK_XPU_ID'):
    old = (f'\treturn scm->ScmSipSysCall(scm, {command}, '
           f'{command}_PARAM_ID, Parameters, Results);')
    new = (f'\tEFI_STATUS status = scm->ScmSipSysCall(scm, {command}, '
           f'{command}_PARAM_ID, Parameters, Results);\n'
           f'\tPrint(u"PAS %u {command}: transport=%r raw=%lx/%lx/%lx\\n",\n'
           '\t      pas_id, status, Results[0], Results[1], Results[2]);\n'
           '\tCopyMem(sq2_pas_result, Results, sizeof(sq2_pas_result));\n'
           '\treturn status;')
    edit('src/scm.c', old, new)
scm_path = a.output / 'src/scm.c'
scm_text = scm_path.read_text()
assert scm_text.count('UINT64 Results[SCM_MAX_NUM_RESULTS];') == 4
scm_path.write_text(scm_text.replace('UINT64 Results[SCM_MAX_NUM_RESULTS];',
    'UINT64 Results[SCM_MAX_NUM_RESULTS] = {~(UINT64)0, ~(UINT64)0, ~(UINT64)0};'))

edit('src/pil.c', '#include "qebspil.h"', '#include "qebspil.h"\n#include <libfdt.h>')
edit('src/pil.c', '''		if (!EFI_ERROR(status))
			found = EFI_SUCCESS;''', '''		if (!EFI_ERROR(status)) {
			/* Publish only after authentication and release from reset succeed. */
			if (fdt_setprop_inplace_u32(loaded_pil[i].dtb, loaded_pil[i].dt_node,
				"microsoft,el2-preboot-complete", 1))
				return EFI_INVALID_PARAMETER;
			found = EFI_SUCCESS;
			sq2_preboot_note(loaded_pil[i].dtb, loaded_pil[i].dt_node,
				11, status, sq2_pas_result);
		} else {
			sq2_preboot_note(loaded_pil[i].dtb, loaded_pil[i].dt_node,
				12, status, NULL);
			/* Preserve failure rather than hiding it behind another DSP. */
			return status;
		}''')
# Upstream's current failure delay is 500 seconds (not 500 ms).
edit('src/main.c', 'BS->Stall(500 * 1000 * 1000)', 'BS->Stall(5 * 1000 * 1000)')
edit('src/main.c', 'return BS->Stall(5 * 1000 * 1000);',
     'Print(u"Preboot PAS failed; rebooting to original default in 5 seconds.\\n");\n'
     '\tBS->Stall(5 * 1000 * 1000);\n'
     '\tRT->ResetSystem(EfiResetCold, status, 0, NULL);\n\treturn status;')
# EBS may be retried; never authenticate/reset an already-started DSP twice.
edit('src/pil.c', '\t\tstatus = pil_finish(&loaded_pil[i]);', '''
		{
			int len;
			const fdt32_t *done = fdt_getprop(loaded_pil[i].dtb,
				loaded_pil[i].dt_node, "microsoft,el2-preboot-complete", &len);
			if (done && len == sizeof(*done) && fdt32_to_cpu(*done) == 1) {
				found = EFI_SUCCESS;
				continue;
			}
		}
		sq2_preboot_note(loaded_pil[i].dtb, loaded_pil[i].dt_node,
			10, EFI_SUCCESS, NULL);
		status = pil_finish(&loaded_pil[i]);''')
(a.output / 'SQ2-PREPARATION.txt').write_text(
    f'qebspil {head}\nBuild-only: requires matching kernel takeover; not deployed.\n')
# Bracket each secure call: "Starting remoteproc" alone does not locate a hang.
for call, args in [
    ('init', 'pil->type->id[c].full, fw->metadata'),
    ('mem_setup', 'pil->type->id[c].full, fw->mem_addr, fw->mem_size'),
    ('start', 'pil->type->id[c].full'),
]:
    line = f'\t\tstatus = scm_pil_{call}({args});'
    edit('src/pil.c', line,
         f'\t\tPrint(u"PAS %u {call} begin\\n", pil->type->id[c].full);\n' + line +
         f'\n\t\tPrint(u"PAS %u {call} returned %r\\n", pil->type->id[c].full, status);')
edit('src/main.c', '\tstatus = pil_finish_all();',
     '\tstatus = pil_finish_all();\n\tPrint(u"SQ2 all PAS finish returned %r\\n", status);')
edit('src/main.c', '\tif (!EFI_ERROR(status))\n\t\treturn status;',
     '\tif (!EFI_ERROR(status)) {\n'
     '\t\tstatus = sq2_preboot_publish_results();\n'
     '\t\tPrint(u"SQ2 kernel DT handoff returned %r\\n", status);\n'
     '\t\tif (!EFI_ERROR(status)) return status;\n\t}')
if venus_metadata is not None:
    # Preserve the already deployed DSP trace and Venus call order together.
    edit('Makefile', '\tsrc/sq2-preboot-rmtfs.o \\',
         '\tsrc/sq2-preboot-rmtfs.o \\\n\tsrc/sq2-venus-preboot.o \\')
    (a.output / 'src/sq2-venus-preboot.c').write_bytes(
        (ROOT / 'probe/virtualization/sq2-venus-preboot.c').read_bytes().replace(b'\r\n', b'\n'))
    (a.output / 'src/sq2-venus-metadata.h').write_text(
        'static const unsigned char sq2_venus_metadata[16384] = {\n' +
        ','.join(hex(byte) for byte in venus_metadata) + '\n};\n')
    edit('src/qebspil.h', '#endif', '''extern UINT64 sq2_pas_trace[3][4][5];
EFI_STATUS sq2_venus_prepare(void *dtb);
EFI_STATUS sq2_venus_finish(void);
EFI_STATUS sq2_venus_publish(void *dest);
#endif''')
    edit('src/main.c', '\tstatus = dtb_enumerate_rprocs(dtb);',
         '\tstatus = sq2_venus_prepare(dtb);\n\tif (EFI_ERROR(status)) return status;\n'
         '\tstatus = dtb_enumerate_rprocs(dtb);')
    edit('src/main.c', '\tif (!EFI_ERROR(status)) {\n\t\tstatus = sq2_preboot_publish_results();',
         '\tif (!EFI_ERROR(status)) status = sq2_venus_finish();\n'
         '\tif (!EFI_ERROR(status)) {\n\t\tstatus = sq2_preboot_publish_results();')
    edit('src/scm.c', 'EFI_STATUS scm_init(void)', '''UINT64 sq2_pas_trace[3][4][5];
static void sq2_record_pas(UINT8 pas_id, unsigned op, EFI_STATUS status,
                           const UINT64 *results)
{
    unsigned row, i;
    switch (pas_id) {
    case 1: row = 0; break;
    case 4: row = 1; break;
    case 18: row = 2; break;
    default: return;
    }
    if (op >= 4) return;
    sq2_pas_trace[row][op][0] = status;
    for (i = 0; i < 4; i++) sq2_pas_trace[row][op][i + 1] = results[i];
}

EFI_STATUS scm_init(void)''')
    edit('src/scm.c', '\tstatus = LibLocateProtocol(',
         '\tSetMem(sq2_pas_trace, sizeof(sq2_pas_trace), 0xff);\n\tstatus = LibLocateProtocol(')
    for op, command in enumerate(('TZ_PIL_INIT_ID', 'TZ_PIL_MEM_ID',
                                  'TZ_PIL_AUTH_RESET_ID', 'TZ_PIL_UNLOCK_XPU_ID')):
        line = (f'\tEFI_STATUS status = scm->ScmSipSysCall(scm, {command}, '
                f'{command}_PARAM_ID, Parameters, Results);')
        edit('src/scm.c', line, line + f'\n\tsq2_record_pas(pas_id, {op}, status, Results);')
    path = a.output / 'src/scm.c'
    text = path.read_text()
    old = '= {~(UINT64)0, ~(UINT64)0, ~(UINT64)0};'
    assert text.count(old) == 4
    path.write_text(text.replace(old, '= {~(UINT64)0, ~(UINT64)0, ~(UINT64)0, ~(UINT64)0};'))
    edit('src/sq2-preboot-rmtfs.c', '\tWriteBackInvalidateDataCacheRange(dest, fdt_totalsize(dest));', '''    {
        fdt64_t trace[3][4][5];
        unsigned row, op, word;
        for (row = 0; row < 3; row++)
            for (op = 0; op < 4; op++)
                for (word = 0; word < 5; word++)
                    trace[row][op][word] = cpu_to_fdt64(sq2_pas_trace[row][op][word]);
        if (fdt_setprop_inplace(dest, 0, "microsoft,el2-pas-trace", trace, sizeof(trace)))
            return EFI_INVALID_PARAMETER;
    }
	if (EFI_ERROR(sq2_venus_publish(dest))) return EFI_INVALID_PARAMETER;
	WriteBackInvalidateDataCacheRange(dest, fdt_totalsize(dest));''')
print(a.output)
