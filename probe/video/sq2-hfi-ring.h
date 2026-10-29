/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SQ2_HFI_RING_H
#define SQ2_HFI_RING_H
#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/compiler.h>
#define SQ2_RING_READ(v) READ_ONCE(v)
#define SQ2_RING_WRITE(v, x) WRITE_ONCE(v, x)
#else
#include <stdint.h>
typedef uint32_t u32;
#define SQ2_RING_READ(v) (v)
#define SQ2_RING_WRITE(v, x) ((v) = (x))
#endif
#define SQ2_HFI_MAX_PACKET_BYTES 12288

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
 if (p->bytes < 8 || p->bytes > SQ2_HFI_MAX_PACKET_BYTES || p->bytes % 4) return -1;
 words = p->bytes / 4;
 if (words > available) return -1;
 p->type = SQ2_RING_READ(ring[(r + 1) % capacity]);
 p->word2 = words >= 3 ? SQ2_RING_READ(ring[(r + 2) % capacity]) : ~(u32)0;
 p->word3 = words >= 4 ? SQ2_RING_READ(ring[(r + 3) % capacity]) : ~(u32)0;
 p->next = (r + words) % capacity;
 return 1;
}
/* Copy one packet already validated by sq2_hfi_packet_read, wrapping at the
 * ring end. Each word is read once; the copy, not the shared ring, is parsed.
 */
static inline int sq2_hfi_packet_copy(const u32 *ring, u32 capacity, u32 r,
                                      const struct sq2_hfi_packet *p,
                                      u32 *dst, u32 dst_words)
{
 u32 i, words = p->bytes / 4;
 if (r >= capacity || words > dst_words || words > capacity) return -1;
 for (i = 0; i < words; i++) dst[i] = SQ2_RING_READ(ring[(r + i) % capacity]);
 /* The firmware could change the length word between the check and copy. */
 return dst[0] == p->bytes ? (int)words : -1;
}
/* Producer side for host commands. One word stays free so r == w is empty.
 * Returns the new write index; the caller publishes it after a write barrier.
 */
static inline int sq2_hfi_packet_write(u32 *ring, u32 capacity, u32 r, u32 w,
                                       const u32 *src, u32 words, u32 *next)
{
 u32 used, i;
 if (capacity < 4 || capacity > 0x10000000 || r >= capacity || w >= capacity ||
     words < 2 || words * 4 > SQ2_HFI_MAX_PACKET_BYTES || src[0] != words * 4)
  return -1;
 used = w >= r ? w - r : capacity - r + w;
 if (words > capacity - 1 - used) return -1;
 for (i = 0; i < words; i++) SQ2_RING_WRITE(ring[(w + i) % capacity], src[i]);
 *next = (w + words) % capacity;
 return 0;
}
#undef SQ2_RING_READ
#undef SQ2_RING_WRITE
#endif
