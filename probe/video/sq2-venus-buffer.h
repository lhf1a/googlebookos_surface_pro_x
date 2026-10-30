/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SQ2_VENUS_BUFFER_H
#define SQ2_VENUS_BUFFER_H
/* Full-width checks BEFORE assigning a DMA address to a 32-bit HFI field.
 * This is the tested nonsecure intersection, not a change to SCM thresholds.
 */
static inline int sq2_venus_buffer_valid(unsigned long long addr,
                                        unsigned long long size)
{
 return size && addr >= 0x25800000ULL && addr < 0xc0000000ULL &&
        size <= 0xc0000000ULL - addr;
}
static inline int sq2_venus_payload_valid(unsigned long long length,
                                         unsigned long long used,
                                         unsigned long long offset)
{
 return offset <= used && used <= length;
}
#endif
