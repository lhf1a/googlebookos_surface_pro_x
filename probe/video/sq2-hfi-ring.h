/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SQ2_HFI_RING_H
#define SQ2_HFI_RING_H
#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/compiler.h>
#define SQ2_RING_READ(v) READ_ONCE(v)
#else
#include <stdint.h>
typedef uint32_t u32;
#define SQ2_RING_READ(v) (v)
#endif

struct sq2_hfi_packet {
 u32 bytes, type, word2, word3, next;
};
/* Pure bounds check/parser; caller supplies the firmware producer index after
 * the DMA read barrier and commits the returned consumer index separately.
 */
static inline int sq2_hfi_packet_read(const u32 *ring, u32 capacity,
                                    u32 r, u32 w, struct sq2_hfi_packet *p)
{
 u32 available, words;
 if (capacity < 4 || capacity > 0x10000000 || r >= capacity || w >= capacity)
  return -1;
 if (r == w) return 0;
 available = w >= r ? w - r : capacity - r + w;
 p->bytes = SQ2_RING_READ(ring[r]);
 if (p->bytes < 8 || p->bytes > 12288 || p->bytes % 4) return -1;
 words = p->bytes / 4;
 if (words > available) return -1;
 p->type = SQ2_RING_READ(ring[(r + 1) % capacity]);
 p->word2 = words >= 3 ? SQ2_RING_READ(ring[(r + 2) % capacity]) : ~(u32)0;
 p->word3 = words >= 4 ? SQ2_RING_READ(ring[(r + 3) % capacity]) : ~(u32)0;
 p->next = (r + words) % capacity;
 return 1;
}
#undef SQ2_RING_READ
#endif
