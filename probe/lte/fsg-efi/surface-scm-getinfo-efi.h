/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef SURFACE_SCM_GETINFO_EFI_H
#define SURFACE_SCM_GETINFO_EFI_H
#include <efi.h>
#include "external/EFIScm.h"
#include "surface-scm-storage-read.h"

/* Read-only pre-EL2 storage geometry for current-device FSG collection.
 * Build against the pinned qebspil EFI protocol header, not a guessed vtable.
 * Only geometry and status leave the private allocation; no serial/model data.
 */
struct spx_scm_info_report {
 UINT64 revision, results[SCM_MAX_NUM_RESULTS], returned_size;
 EFI_STATUS allocation_status, call_status, free_status;
 UINT32 current_el, calls, candidate_valid;
 enum spx_storage_info_check check;
 struct spx_storage_geometry geometry;

};
EFI_STATUS spx_scm_get_info(EFI_BOOT_SERVICES *bs, QCOM_SCM_PROTOCOL *scm,
                          struct spx_scm_info_report *report);
#endif
