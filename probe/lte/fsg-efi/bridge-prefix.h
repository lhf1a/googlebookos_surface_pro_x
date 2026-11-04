/* SPDX-License-Identifier: BSD-3-Clause */
/* ABI prefix only, derived from pinned EFIShmBridge.h revision 0x10001.
 * No create/delete or arbitrary VM permission interface is exposed here.
 */
#ifndef SPX_BRIDGE_PREFIX_H
#define SPX_BRIDGE_PREFIX_H
#include <efi.h>
#include <stddef.h>
enum spx_shm_type { SPX_SHM_DEFAULT, SPX_SHM_LOADER, SPX_SHM_BOOT, SPX_SHM_RUNTIME };
struct spx_shm_prefix {
 UINT64 Revision;
 VOID *(EFIAPI *Allocate)(struct spx_shm_prefix *, UINT64 *, enum spx_shm_type);
 EFI_STATUS (EFIAPI *Free)(struct spx_shm_prefix *, VOID *);
};
_Static_assert(sizeof(enum spx_shm_type)==4, "SHM enum ABI");
_Static_assert(offsetof(struct spx_shm_prefix,Allocate)==8, "SHM allocation slot");
_Static_assert(offsetof(struct spx_shm_prefix,Free)==16, "SHM free slot");
#endif
