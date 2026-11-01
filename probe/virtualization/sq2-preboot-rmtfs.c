// SPDX-License-Identifier: GPL-2.0-only
/* SC8180X-only EL1 ownership handoff. No disk access or RMTFS data writes. */
#include <efi.h>
#include <efilib.h>
#include <libfdt.h>
#include "qebspil.h"
#include "external/CacheMaintenanceLib.h"

struct sq2_vmperm {
	UINT32 vmid, perm;
	UINT64 ctx;
	UINT32 ctx_size, unused;
};
struct sq2_assignment {
	UINT32 src[16];
	UINT64 region[8];
	struct sq2_vmperm dest[2];
};
_Static_assert(sizeof(struct sq2_vmperm) == 24, "SCM permission ABI");

static EFI_PHYSICAL_ADDRESS assignment_page;
static void *result_dtb;
static int result_node;
static BOOLEAN assigned;

EFI_STATUS sq2_preboot_note(void *dtb, int node, UINT32 stage,
                           EFI_STATUS status, const UINT64 *result)
{
	fdt32_t cells[6] = {
		cpu_to_fdt32(stage), cpu_to_fdt32((UINT32)status),
		cpu_to_fdt32((UINT64)status >> 32),
		cpu_to_fdt32(result ? result[0] : 0),
		cpu_to_fdt32(result ? result[1] : 0),
		cpu_to_fdt32(result ? result[2] : 0),
	};
	int err = fdt_setprop_inplace(dtb, node, "microsoft,el2-preboot-status",
				    cells, sizeof(cells));
	/* slbounce flushes BEFORE real EBS. Publish our late DT changes again
	 * before its EL2 transition, which does not preserve dirty caches. */
	WriteBackInvalidateDataCacheRange(dtb, fdt_totalsize(dtb));
	return err ? EFI_INVALID_PARAMETER : EFI_SUCCESS;
}

EFI_STATUS sq2_rmtfs_prepare(void *dtb)
{
	const fdt32_t *reg, *value;
	int len, node;
	EFI_STATUS status;
	struct sq2_assignment *a;

	if (assignment_page) {
		BS->FreePages(assignment_page, 1);
		assignment_page = 0;
	}
	result_dtb = NULL;
	assigned = FALSE;
	if (fdt_node_check_compatible(dtb, 0, "microsoft,surface-pro-x"))
		return EFI_UNSUPPORTED;
	node = fdt_path_offset(dtb, "/reserved-memory/memory@85500000");
	if (node < 0 || fdt_node_check_compatible(dtb, node, "qcom,rmtfs-mem") ||
	    !fdt_getprop(dtb, node, "microsoft,el2-preboot-rmtfs", NULL))
		return EFI_NOT_FOUND;
	reg = fdt_getprop(dtb, node, "reg", &len);
	if (!reg || len != 16 || fdt32_to_cpu(reg[0]) ||
	    fdt32_to_cpu(reg[1]) != 0x85500000 || fdt32_to_cpu(reg[2]) ||
	    fdt32_to_cpu(reg[3]) != 0x200000)
		return EFI_INVALID_PARAMETER;
	value = fdt_getprop(dtb, node, "qcom,vmid", &len);
	if (!value || len != 4 || fdt32_to_cpu(*value) != 15)
		return EFI_INVALID_PARAMETER;
	value = fdt_getprop(dtb, node, "qcom,client-id", &len);
	if (!value || len != 4 || fdt32_to_cpu(*value) != 1)
		return EFI_INVALID_PARAMETER;
	value = fdt_getprop(dtb, node, "microsoft,el2-preboot-complete", &len);
	if (!value || len != 4 || fdt32_to_cpu(*value) != 0)
		return EFI_INVALID_PARAMETER;
	status = BS->AllocatePages(AllocateAnyPages, EfiBootServicesData, 1, &assignment_page);
	if (EFI_ERROR(status))
		return status;
	a = (void *)assignment_page;
	ZeroMem(a, EFI_PAGE_SIZE);
	a->src[0] = 3; /* QCOM_SCM_VMID_HLOS */
	a->region[0] = 0x85500000;
	a->region[1] = 0x200000;
	a->dest[0].vmid = 3;
	a->dest[1].vmid = 15; /* QCOM_SCM_VMID_MSS_MSA */
	a->dest[0].perm = a->dest[1].perm = 6; /* read/write, as normal Linux */
	result_dtb = dtb;
	result_node = node;
	return sq2_preboot_note(dtb, node, 1, EFI_SUCCESS, NULL);
}

EFI_STATUS sq2_rmtfs_finish(void)
{
	struct sq2_assignment *a = (void *)assignment_page;
	EFI_STATUS status;
	UINT64 result[4] = {0};
	if (!result_dtb || !a)
		return EFI_NOT_READY;
	if (assigned)
		return EFI_SUCCESS;
	sq2_preboot_note(result_dtb, result_node, 2, EFI_SUCCESS, NULL);
	WriteBackInvalidateDataCacheRange(a, EFI_PAGE_SIZE);
	status = sq2_scm_assign((UINT64)a->region, 16, (UINT64)a->src, 4,
			      (UINT64)a->dest, sizeof(a->dest), result);
	sq2_preboot_note(result_dtb, result_node, 3, status, result);
	Print(u"SQ2 RMTFS preboot assignment: %r\n", status);
	if (EFI_ERROR(status))
		return status;
	if (fdt_setprop_inplace_u32(result_dtb, result_node,
				  "microsoft,el2-preboot-complete", 1))
		return EFI_INVALID_PARAMETER;
	assigned = TRUE;
	return sq2_preboot_note(result_dtb, result_node, 4, EFI_SUCCESS, result);
}

/* The EFI stub supplies its final DT before EBS. Publish while still at EL1;
 * no parsing of the original EFI DT is needed after the EL2 transition. */
EFI_STATUS sq2_preboot_publish_results(void)
{
	static const char *paths[] = {
		"/soc@0/remoteproc@17300000", "/soc@0/remoteproc@8300000",
		"/soc@0/remoteproc@4080000", "/reserved-memory/memory@85500000",
	};
	const fdt64_t *address;
	void *dest;
	int len, i, src_nodes[4], dst_nodes[4];
	fdt32_t traces[4][6];
	if (!result_dtb || !assigned)
		return EFI_NOT_READY;
	address = fdt_getprop(result_dtb, 0, "microsoft,el2-kernel-fdt", &len);
	if (!address || len != 8 || !fdt64_ld(address))
		return EFI_INVALID_PARAMETER;
	dest = (void *)(UINTN)fdt64_ld(address);
	if (dest == result_dtb || ((UINTN)dest & 7) || fdt_check_header(dest) ||
	    fdt_totalsize(dest) > 2 * 1024 * 1024 ||
	    fdt_node_check_compatible(dest, 0, "microsoft,surface-pro-x"))
		return EFI_INVALID_PARAMETER;
	/* Validate every result first. Never turn a missing/failed PAS into success. */
	for (i = 0; i < 4; i++) {
		const char *opt = i == 3 ? "microsoft,el2-preboot-rmtfs" : "microsoft,el2-preboot";
		const fdt32_t *done;
		const void *trace;
		src_nodes[i] = fdt_path_offset(result_dtb, paths[i]);
		dst_nodes[i] = fdt_path_offset(dest, paths[i]);
		if (src_nodes[i] < 0 || dst_nodes[i] < 0 ||
		    !fdt_getprop(result_dtb, src_nodes[i], opt, NULL) ||
		    !fdt_getprop(dest, dst_nodes[i], opt, NULL))
			return EFI_INVALID_PARAMETER;
		done = fdt_getprop(result_dtb, src_nodes[i], "microsoft,el2-preboot-complete", &len);
		if (!done || len != 4 || fdt32_to_cpu(*done) != 1)
			return EFI_NOT_READY;
		done = fdt_getprop(dest, dst_nodes[i], "microsoft,el2-preboot-complete", &len);
		if (!done || len != 4 || fdt32_to_cpu(*done) > 1)
			return EFI_INVALID_PARAMETER;
		trace = fdt_getprop(result_dtb, src_nodes[i], "microsoft,el2-preboot-status", &len);
		if (!trace || len != sizeof(traces[i])) return EFI_INVALID_PARAMETER;
		/* GNU-EFI's legacy CopyMem source parameter is not const-qualified. */
		CopyMem(traces[i], (void *)trace, sizeof(traces[i]));
		trace = fdt_getprop(dest, dst_nodes[i], "microsoft,el2-preboot-status", &len);
		if (!trace || len != sizeof(traces[i])) return EFI_INVALID_PARAMETER;
	}
	for (i = 0; i < 4; i++) {
		if (fdt_setprop_inplace(dest, dst_nodes[i], "microsoft,el2-preboot-status",
				traces[i], sizeof(traces[i])) ||
		    fdt_setprop_inplace_u32(dest, dst_nodes[i], "microsoft,el2-preboot-complete", 1))
			return EFI_INVALID_PARAMETER;
	}
	WriteBackInvalidateDataCacheRange(dest, fdt_totalsize(dest));
	return EFI_SUCCESS;
}
