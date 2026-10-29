/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../probe/video/sq2-hfi-ring.h"
int main(void)
{
 u32 ring[16] = {0};
 struct sq2_hfi_packet p;
 unsigned int r, words, i, cases = 0;
 /* Every legal start/length, including wrap across the last ring word. */
 for (r = 0; r < 16; r++) for (words = 2; words < 16; words++) {
  for (i = 0; i < words; i++) ring[(r + i) % 16] = 0x70000000 + i;
  ring[r] = words * 4;
  assert(sq2_hfi_packet_read(ring, 16, r, (r + words) % 16, &p) == 1);
  assert(p.bytes == words * 4 && p.type == 0x70000001);
  assert(p.next == (r + words) % 16);
  assert(p.word2 == (words >= 3 ? 0x70000002 : UINT32_MAX));
  assert(p.word3 == (words >= 4 ? 0x70000003 : UINT32_MAX));
  /* Producer has not committed the final word: reject incomplete packet. */
  assert(sq2_hfi_packet_read(ring, 16, r, (r + words - 1) % 16, &p) == -1);
  cases += 2;
 }
 assert(sq2_hfi_packet_read(NULL, 16, 16, 0, &p) == -1);
 assert(sq2_hfi_packet_read(NULL, 16, 0, UINT32_MAX, &p) == -1);
 assert(sq2_hfi_packet_read(NULL, 16, 4, 4, &p) == 0);
 assert(sq2_hfi_packet_read(NULL, 3, 0, 1, &p) == -1);
 assert(sq2_hfi_packet_read(NULL, UINT32_MAX, 0, 1, &p) == -1);
 for (i = 0; i < 12300; i++) {
  u32 *large = calloc(4096, sizeof(u32));
  assert(large);
  large[0] = i;
  assert(sq2_hfi_packet_read(large, 4096, 0, 4095, &p) ==
         (i >= 8 && i <= 12288 && i % 4 == 0 ? 1 : -1));
  free(large);
 }
 /* Full copy and host producer: every start/length/occupancy, with wrap. */
 {
  u32 dst[16], src[16], next, used, copies = 0, writes = 0;
  for (r = 0; r < 16; r++) for (words = 2; words < 16; words++) {
   for (i = 0; i < words; i++) ring[(r + i) % 16] = 0x71000000 + i;
   ring[r] = words * 4;
   assert(sq2_hfi_packet_read(ring, 16, r, (r + words) % 16, &p) == 1);
   assert(sq2_hfi_packet_copy(ring, 16, r, &p, dst, 16) == (int)words);
   for (i = 1; i < words; i++) assert(dst[i] == 0x71000000 + i);
   assert(sq2_hfi_packet_copy(ring, 16, r, &p, dst, words - 1) == -1);
   ring[r] = words * 4 + 4; /* producer changed length after the check */
   assert(sq2_hfi_packet_copy(ring, 16, r, &p, dst, 16) == -1);
   copies += 3;
   for (used = 0; used < 16; used++) {
    u32 w = (r + used) % 16;
    for (i = 0; i < words; i++) src[i] = 0x72000000 + i;
    src[0] = words * 4;
    memset(ring, 0, sizeof(ring));
    if (words <= 15 - used) {
     assert(sq2_hfi_packet_write(ring, 16, r, w, src, words, &next) == 0);
     assert(next == (w + words) % 16);
     for (i = 0; i < words; i++) assert(ring[(w + i) % 16] == src[i]);
     /* Never reaches the consumer index: r == w stays "empty". */
     assert(next != r || words == 0);
    } else {
     assert(sq2_hfi_packet_write(ring, 16, r, w, src, words, &next) == -1);
     for (i = 0; i < 16; i++) assert(!ring[i]);
    }
    writes++;
   }
  }
  src[0] = 8; src[1] = 1;
  assert(sq2_hfi_packet_write(ring, 16, 0, 0, src, 3, &next) == -1); /* size mismatch */
  assert(sq2_hfi_packet_write(ring, 16, 16, 0, src, 2, &next) == -1);
  assert(sq2_hfi_packet_write(ring, 16, 0, 0, src, 1, &next) == -1);
  printf("SQ2_HFI_RING_COPY_WRITE_PASS copies=%u writes=%u\n", copies, writes);
 }
 printf("SQ2_HFI_RING_PASS wrap/incomplete=%u invalid indices and 12300 lengths\n", cases);
 return 0;
}
