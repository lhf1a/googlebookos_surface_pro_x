/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SQ2_HFI_DEC_H
#define SQ2_HFI_DEC_H
/* Pure helpers for the private decode trial: buffer-requirement parsing,
 * pinned 6.3 Venus NV12/UBWC size formulas and bounded plane CRC. Firmware
 * packets are untrusted; every index is checked against the copied length.
 */
#include "sq2-hfi-ring.h"
#ifdef __KERNEL__
#include <linux/crc32.h>
#define SQ2_CRC32(c, p, n) crc32_le(c, p, n)
#else
#include <stddef.h>
typedef uint8_t u8;
static inline u32 SQ2_CRC32(u32 c, const u8 *p, size_t n)
{
 while (n--) {
  int k;
  c ^= *p++;
  for (k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320U & -(c & 1));
 }
 return c;
}
#endif
#define SQ2_ALIGN(x, a) (((x) + (a) - 1) / (a) * (a))
#define SQ2_DIV_UP(x, a) (((x) + (a) - 1) / (a))

struct sq2_bufreq {
 u32 type, size, region_size, hold_count, count_min, count_actual, contiguous, alignment;
};
/* SESSION_PROPERTY_INFO {size, 0x221009, sid, num, 0x202001, bufreq[8]...}. */
static inline int sq2_parse_bufreq(const u32 *pkt, u32 words, struct sq2_bufreq *out, u32 max)
{
 u32 i, n;
 if (words < 5 || words * 4 > SQ2_HFI_MAX_PACKET_BYTES || pkt[0] != words * 4 ||
     pkt[1] != 0x221009 || pkt[3] != 1 || pkt[4] != 0x202001 ||
     (words - 5) % 8)
  return -1;
 n = (words - 5) / 8;
 if (!n || n > max) return -1;
 for (i = 0; i < n; i++) {
  const u32 *s = pkt + 5 + i * 8;
  out[i].type = s[0]; out[i].size = s[1]; out[i].region_size = s[2];
  out[i].hold_count = s[3]; out[i].count_min = s[4]; out[i].count_actual = s[5];
  out[i].contiguous = s[6]; out[i].alignment = s[7];
 }
 return (int)n;
}
/* get_framesize_raw_nv12 and the matching plane layout. */
static inline u32 sq2_nv12_size(u32 w, u32 h, u32 *y_stride, u32 *y_scan)
{
 u32 ys = SQ2_ALIGN(w, 128), ysl = SQ2_ALIGN(h, 32), uvsl = SQ2_ALIGN((h + 1) >> 1, 16);
 if (y_stride) *y_stride = ys;
 if (y_scan) *y_scan = ysl;
 return SQ2_ALIGN(ys * ysl + ys * uvsl + 4096 + 8192, 4096);
}
/* get_framesize_raw_nv12_ubwc. */
static inline u32 sq2_nv12_ubwc_size(u32 w, u32 h)
{
 u32 ymeta = SQ2_ALIGN(SQ2_ALIGN(SQ2_DIV_UP(w, 32), 64) * SQ2_ALIGN(SQ2_DIV_UP(h, 8), 16), 4096);
 u32 ys = SQ2_ALIGN(w, 128), yplane = SQ2_ALIGN(ys * SQ2_ALIGN(h, 32), 4096);
 u32 uvmeta = SQ2_ALIGN(SQ2_ALIGN(SQ2_DIV_UP(w / 2, 16), 64) * SQ2_ALIGN(SQ2_DIV_UP(h / 2, 8), 16), 4096);
 u32 uvplane = SQ2_ALIGN(ys * SQ2_ALIGN(h / 2, 32), 4096);
 u32 extra = ys * 48 > 16384 ? ys * 48 : 16384;
 return SQ2_ALIGN(ymeta + yplane + uvmeta + uvplane + extra, 4096);
}
/* zlib-compatible CRC32 of rows*width bytes at offset with the given stride.
 * Returns -1 without reading when any row would exceed the buffer.
 */
static inline int sq2_plane_crc(const u8 *buf, size_t len, size_t offset, u32 stride,
                                u32 width, u32 rows, u32 *crc)
{
 u32 r, c = ~0U;
 if (!rows || width > stride || offset > len ||
     (size_t)stride * (rows - 1) + width > len - offset)
  return -1;
 for (r = 0; r < rows; r++) c = SQ2_CRC32(c, buf + offset + (size_t)r * stride, width);
 *crc = ~c;
 return 0;
}
#endif
