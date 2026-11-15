/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SPX_CAMERA_ACPI_H
#define SPX_CAMERA_ACPI_H
/* The captured Surface DSDT is ~400 KiB; no arbitrary physical-address input. */
#define SPX_CAMERA_ACPI_MAX (1024U * 1024U)
static inline int spx_camera_dsdt_address(const unsigned char *p, unsigned int n,
                                         unsigned long long *addr)
{
 if (n < 44 || sq2_acpi_length(p, n, "FACP", SQ2_ACPI_TABLE_MAX) != n ||
     !sq2_acpi_checksum(p, n)) return -1;
 *addr = n >= 148 ? sq2_acpi_u64(p + 140) : 0;
 if (!*addr) *addr = sq2_acpi_u32(p + 40);
 return *addr ? 0 : -1;
}
#endif
