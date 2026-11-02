/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SQ2_IPA_ACPI_H
#define SQ2_IPA_ACPI_H
/* Pure bounds/checksum helpers, shared by kernel read-only export and host tests. */
#define SQ2_ACPI_HEADER_SIZE 36U
#define SQ2_ACPI_TABLE_MAX 65536U
#define SQ2_ACPI_XSDT_MAX (SQ2_ACPI_HEADER_SIZE + 256U * 8U)
static inline unsigned int sq2_acpi_u32(const unsigned char *p)
{
 return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
        ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}
static inline unsigned long long sq2_acpi_u64(const unsigned char *p)
{
 return sq2_acpi_u32(p) | ((unsigned long long)sq2_acpi_u32(p + 4) << 32);
}
static inline int sq2_acpi_checksum(const unsigned char *p, unsigned int n)
{
 unsigned int i;
 unsigned char sum = 0;
 for (i = 0; i < n; i++) sum += p[i];
 return sum == 0;
}
static inline int sq2_acpi_rsdp(const unsigned char *p, unsigned int n,
                              unsigned long long *xsdt)
{
 const char sig[] = "RSD PTR ";
 unsigned int i;
 if (n != 36 || p[15] < 2 || sq2_acpi_u32(p + 20) != 36) return -1;
 for (i = 0; i < 8; i++) if (p[i] != sig[i]) return -1;
 if (!sq2_acpi_checksum(p, 20) || !sq2_acpi_checksum(p, 36)) return -1;
 *xsdt = sq2_acpi_u64(p + 24);
 return *xsdt ? 0 : -1;
}
static inline unsigned int sq2_acpi_length(const unsigned char *p, unsigned int n,
                                         const char *sig, unsigned int max)
{
 unsigned int len, i;
 if (n < SQ2_ACPI_HEADER_SIZE) return 0;
 for (i = 0; i < 4; i++) if (p[i] != sig[i]) return 0;
 len = sq2_acpi_u32(p + 4);
 return len >= SQ2_ACPI_HEADER_SIZE && len <= max ? len : 0;
}
#endif
