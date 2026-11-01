// SPDX-License-Identifier: GPL-2.0-only
/* PAS9 metadata only: Linux retains segment loading, power and firmware ownership. */
#include <efi.h>
#include <efilib.h>
#include <libfdt.h>
#include "qebspil.h"
#include "external/CacheMaintenanceLib.h"
#include "sq2-venus-metadata.h"

static struct pil_fw venus;
static void *venus_dtb;
static BOOLEAN venus_touched, venus_ready;
static const UINT32 venus_contract[] = {0, 9, 0x8c000000, 0x500000};

static BOOLEAN venus_check_contract(const void *dtb, UINT32 state)
{
 const fdt32_t *p;
 int len, i;
 if (fdt_node_check_compatible(dtb, 0, "microsoft,surface-pro-x") ||
     fdt_node_check_compatible(dtb, 0, "qcom,sc8180x")) return FALSE;
 p = fdt_getprop(dtb, 0, "microsoft,el2-venus-prepared", &len);
 if (!p || len != sizeof(venus_contract) || fdt32_to_cpu(p[0]) != state)
  return FALSE;
 for (i = 1; i < 4; i++)
  if (fdt32_to_cpu(p[i]) != venus_contract[i]) return FALSE;
 return TRUE;
}

EFI_STATUS sq2_venus_prepare(void *dtb)
{
 EFI_FILE_HANDLE root;
 EFI_STATUS status;
 if (venus_touched) return EFI_ACCESS_DENIED;
 fw_free(&venus);
 ZeroMem(&venus, sizeof(venus));
 venus_dtb = NULL;
 venus_ready = FALSE;
 if (!venus_check_contract(dtb, 0)) return EFI_INVALID_PARAMETER;
 root = LibOpenRoot(LoadedImage->DeviceHandle);
 if (!root) return EFI_NOT_FOUND;
 status = fw_prepare(&venus, root, "qcom/venus-5.2/venus.mbn");
 root->Close(root);
 if (EFI_ERROR(status)) return status;
 if (venus.elf_size != 1159200) return EFI_SECURITY_VIOLATION;
 status = fw_check(&venus);
 if (EFI_ERROR(status)) return status;
 ZeroMem((void *)venus.metadata, 4 * EFI_PAGE_SIZE);
 status = fw_load_metadata(&venus);
 if (EFI_ERROR(status)) return status;
 /* Generated only from the pinned image. No executable segments enter PAS here. */
 if (CompareMem((void *)venus.metadata, (void *)sq2_venus_metadata,
                sizeof(sq2_venus_metadata))) return EFI_SECURITY_VIOLATION;
 venus_dtb = dtb;
 return EFI_SUCCESS;
}

static BOOLEAN venus_scm_ok(EFI_STATUS status)
{
 /* EFI protocol returns common status=1 and result=0, unlike Linux SCM. */
 return !EFI_ERROR(status) && sq2_pas_result[0] == 1 && sq2_pas_result[1] == 0;
}

EFI_STATUS sq2_venus_finish(void)
{
 EFI_STATUS status;
 fdt32_t cells[4];
 int i;
 if (venus_ready) return EFI_SUCCESS;
 if (!venus_dtb || venus_touched || !venus_check_contract(venus_dtb, 0))
  return EFI_NOT_READY;
 venus_touched = TRUE;
 status = scm_pil_init(9, venus.metadata);
 if (!venus_scm_ok(status)) return EFI_DEVICE_ERROR;
 status = scm_pil_mem_setup(9, 0x8c000000, 0x500000);
 if (!venus_scm_ok(status)) return EFI_DEVICE_ERROR;
 for (i = 0; i < 4; i++) cells[i] = cpu_to_fdt32(venus_contract[i]);
 cells[0] = cpu_to_fdt32(1);
 if (fdt_setprop_inplace(venus_dtb, 0, "microsoft,el2-venus-prepared", cells, sizeof(cells)))
  return EFI_INVALID_PARAMETER;
 WriteBackInvalidateDataCacheRange(venus_dtb, fdt_totalsize(venus_dtb));
 venus_ready = TRUE;
 Print(u"SQ2 VENUS PAS9 metadata prepared; Linux loads and authenticates\n");
 return EFI_SUCCESS;
}

EFI_STATUS sq2_venus_publish(void *dest)
{
 const void *cells;
 int len;
 if (!venus_ready || !venus_check_contract(venus_dtb, 1) ||
     (!venus_check_contract(dest, 0) && !venus_check_contract(dest, 1)))
  return EFI_NOT_READY;
 cells = fdt_getprop(venus_dtb, 0, "microsoft,el2-venus-prepared", &len);
 if (fdt_setprop_inplace(dest, 0, "microsoft,el2-venus-prepared", cells, len))
  return EFI_INVALID_PARAMETER;
 return EFI_SUCCESS;
}
