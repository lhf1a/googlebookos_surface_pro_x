/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../probe/video/sq2-hfi-dec.h"
int main(void)
{
 static u32 pkt[64];
 struct sq2_bufreq req[11];
 u32 ys, ysl, crc, i, cases = 0;
 u8 *buf;
 /* zlib/IEEE check value. */
 assert(~SQ2_CRC32(~0U, (const u8 *)"123456789", 9) == 0xcbf43926);
 /* Pinned 6.3 formulas for the 320x240 vector. */
 assert(sq2_nv12_size(320, 240, &ys, &ysl) == 159744 && ys == 384 && ysl == 256);
 assert(sq2_nv12_ubwc_size(320, 240) % 4096 == 0);
 cases += 3;
 /* Two synthetic bufreq entries; not firmware output. */
 pkt[0] = (5 + 16) * 4; pkt[1] = 0x221009; pkt[2] = 0x53513201; pkt[3] = 1; pkt[4] = 0x202001;
 for (i = 0; i < 16; i++) pkt[5 + i] = 100 + i;
 assert(sq2_parse_bufreq(pkt, 21, req, 11) == 2);
 assert(req[0].type == 100 && req[0].alignment == 107 && req[1].type == 108 && req[1].hold_count == 111);
 assert(sq2_parse_bufreq(pkt, 21, req, 1) == -1);
 pkt[0] = 80;
 assert(sq2_parse_bufreq(pkt, 20, req, 11) == -1);
 pkt[0] = 84; pkt[3] = 2;
 assert(sq2_parse_bufreq(pkt, 21, req, 11) == -1);
 pkt[3] = 1;
 pkt[4] = 0x1001;
 assert(sq2_parse_bufreq(pkt, 21, req, 11) == -1);
 pkt[4] = 0x202001; pkt[0] = 80;
 assert(sq2_parse_bufreq(pkt, 21, req, 11) == -1);
 pkt[0] = 84; pkt[1] = 0x221008;
 assert(sq2_parse_bufreq(pkt, 21, req, 11) == -1);
 pkt[1] = 0x221009; pkt[3] = 0;
 assert(sq2_parse_bufreq(pkt, 21, req, 11) == -1);
 cases += 9;
 /* Plane CRC: strided copy must equal CRC of the packed plane. */
 buf = calloc(1, 384 * 256 + 384 * 128);
 assert(buf);
 for (i = 0; i < 240; i++) memset(buf + i * 384, (int)i, 320);
 {
  u8 *packed = malloc(320 * 240);
  u32 expect;
  assert(packed);
  for (i = 0; i < 240; i++) memset(packed + i * 320, (int)i, 320);
  expect = ~SQ2_CRC32(~0U, packed, 320 * 240);
  assert(sq2_plane_crc(buf, 384 * 384, 0, 384, 320, 240, &crc) == 0 && crc == expect);
  free(packed);
 }
 /* Exactly fitting last row passes; one byte short and overflowing offsets fail. */
 assert(sq2_plane_crc(buf, 384 * 239 + 320, 0, 384, 320, 240, &crc) == 0);
 assert(sq2_plane_crc(buf, 384 * 239 + 319, 0, 384, 320, 240, &crc) == -1);
 assert(sq2_plane_crc(buf, 1000, 1001, 384, 1, 1, &crc) == -1);
 assert(sq2_plane_crc(buf, 1000, 0, 100, 101, 1, &crc) == -1);
 assert(sq2_plane_crc(buf, 1000, 0, 100, 10, 0, &crc) == -1);
 assert(sq2_plane_crc(buf, 384 * 384, 384 * 256, 384, 320, 120, &crc) == 0);
 cases += 7;
 free(buf);
 printf("SQ2_HFI_DEC_PASS cases=%u SYNTHETIC_NOT_FIRMWARE\n", cases);
 return 0;
}
