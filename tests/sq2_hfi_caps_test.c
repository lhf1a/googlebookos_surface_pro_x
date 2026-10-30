/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../probe/video/sq2-hfi-caps.h"
int main(void)
{
 static u32 pkt[SQ2_HFI_MAX_PACKET_BYTES / 4];
 struct sq2_hfi_caps c;
 u32 n = 0, i, cases = 0;
 /* Synthetic body in the pinned 6.3 parser layout; not firmware output. */
 pkt[n++] = 0; pkt[n++] = 0x20001; pkt[n++] = 0; pkt[n++] = 5;
 pkt[n++] = SQ2_HFI_CODEC_SUPPORTED; pkt[n++] = 0x0100000e; pkt[n++] = 0x01000002;
 pkt[n++] = SQ2_HFI_MAX_SESSIONS; pkt[n++] = 16;
 pkt[n++] = SQ2_HFI_CODEC_MASK; pkt[n++] = 0x2; pkt[n++] = 0x2;
 pkt[n++] = SQ2_HFI_CAPABILITY; pkt[n++] = 1; pkt[n++] = 1; pkt[n++] = 96; pkt[n++] = 4096; pkt[n++] = 1;
 pkt[n++] = SQ2_HFI_PROFILE_LEVEL; pkt[n++] = 0;
 pkt[n++] = SQ2_HFI_CODEC_MASK; pkt[n++] = 0x2; pkt[n++] = 0x1; /* encoder only */
 pkt[0] = n * 4;
 assert(sq2_hfi_caps_parse(pkt, n, &c) == 0);
 assert(c.error == 0 && c.num_properties == 5 && c.body_words == n - 4);
 assert(c.dec_codecs == 0x0100000e && c.enc_codecs == 0x01000002);
 assert(c.max_sessions == 16 && c.codec_supported == 1 && c.codec_mask == 2);
 assert(c.h264_decoder_masks == 1 && c.capabilities == 1 && c.profile_levels == 1);
 assert(c.truncated == 0);
 cases++;
 /* An ID in the last word(s) is counted as truncated and never read past. */
 for (i = 0; i < 3; i++) {
  static const u32 ids[] = { SQ2_HFI_CODEC_SUPPORTED, SQ2_HFI_MAX_SESSIONS, SQ2_HFI_CODEC_MASK };
  u32 *tail = malloc(5 * sizeof(u32));
  assert(tail);
  tail[0] = 20; tail[1] = 0x20001; tail[2] = 0; tail[3] = 1; tail[4] = ids[i];
  assert(sq2_hfi_caps_parse(tail, 5, &c) == 0 && c.truncated == 1);
  free(tail);
  cases++;
 }
 /* Header validation. */
 pkt[0] = n * 4 + 4;
 assert(sq2_hfi_caps_parse(pkt, n, &c) == -1);
 pkt[0] = n * 4; pkt[1] = 0x20006;
 assert(sq2_hfi_caps_parse(pkt, n, &c) == -1);
 pkt[1] = 0x20001;
 assert(sq2_hfi_caps_parse(pkt, 3, &c) == -1);
 assert(sq2_hfi_caps_parse(pkt, SQ2_HFI_MAX_PACKET_BYTES / 4 + 1, &c) == -1);
 cases += 4;
 /* Every body length up to the maximum packet, filled with each tracked ID:
  * ASan catches any read beyond the declared word count.
  */
 for (n = 4; n <= SQ2_HFI_MAX_PACKET_BYTES / 4; n += 97) {
  u32 *b = malloc(n * sizeof(u32));
  assert(b);
  for (i = 4; i < n; i++) b[i] = (i % 3) ? SQ2_HFI_CODEC_MASK : SQ2_HFI_CODEC_SUPPORTED;
  b[0] = n * 4; b[1] = 0x20001; b[2] = 0; b[3] = 0;
  assert(sq2_hfi_caps_parse(b, n, &c) == 0);
  free(b);
  cases++;
 }
 printf("SQ2_HFI_CAPS_PASS cases=%u SYNTHETIC_NOT_FIRMWARE\n", cases);
 return 0;
}
