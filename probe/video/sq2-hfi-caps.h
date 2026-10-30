/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SQ2_HFI_CAPS_H
#define SQ2_HFI_CAPS_H
/* Bounded summary of a copied SYS_INIT_DONE body. Mirrors the pinned 6.3
 * hfi_parser.c word scan (every word is compared with known property IDs),
 * so a payload word equal to an ID is also counted, exactly as upstream would.
 * Counts are scan matches, not a decoded property-by-property schema; raw words
 * are logged separately for offline review.
 */
#include "sq2-hfi-ring.h"
#define SQ2_HFI_CODEC_SUPPORTED 0x1009
#define SQ2_HFI_MAX_SESSIONS 0x1010
#define SQ2_HFI_CODEC_MASK 0x100e
#define SQ2_HFI_UNCOMPRESSED_FORMAT 0x1004
#define SQ2_HFI_CAPABILITY 0x1007
#define SQ2_HFI_PROFILE_LEVEL 0x1006
#define SQ2_HFI_BUFFER_ALLOC_MODE 0x20100b
#define SQ2_HFI_CODEC_H264 0x2
#define SQ2_HFI_DOMAIN_DECODER 0x2

struct sq2_hfi_caps {
 u32 error, num_properties, body_words;
 u32 dec_codecs, enc_codecs, max_sessions;
 u32 codec_supported, codec_mask, raw_formats, capabilities, profile_levels, alloc_modes;
 /* CODEC_MASK entries whose mask covers decoder H264. */
 u32 h264_decoder_masks;
 /* ID matches too close to the end to carry their payload; not read. */
 u32 truncated;
};
/* pkt is the copied packet: {bytes, 0x20001, error, num_properties, body...}. */
static inline int sq2_hfi_caps_parse(const u32 *pkt, u32 words, struct sq2_hfi_caps *c)
{
 u32 i, n;
 if (words < 4 || words * 4 > SQ2_HFI_MAX_PACKET_BYTES || pkt[0] != words * 4 ||
     pkt[1] != 0x20001)
  return -1;
 for (i = 0; i < sizeof(*c) / sizeof(u32); i++) ((u32 *)c)[i] = 0;
 c->error = pkt[2];
 c->num_properties = pkt[3];
 c->body_words = n = words - 4;
 pkt += 4;
 for (i = 0; i < n; i++) {
  u32 left = n - i - 1;
  switch (pkt[i]) {
  case SQ2_HFI_CODEC_SUPPORTED:
   if (left < 2) { c->truncated++; break; }
   if (!c->codec_supported++) {
    c->dec_codecs = pkt[i + 1];
    c->enc_codecs = pkt[i + 2];
   }
   break;
  case SQ2_HFI_MAX_SESSIONS:
   if (left < 1) { c->truncated++; break; }
   c->max_sessions = pkt[i + 1];
   break;
  case SQ2_HFI_CODEC_MASK:
   if (left < 2) { c->truncated++; break; }
   c->codec_mask++;
   if ((pkt[i + 1] & SQ2_HFI_CODEC_H264) && (pkt[i + 2] & SQ2_HFI_DOMAIN_DECODER))
    c->h264_decoder_masks++;
   break;
  case SQ2_HFI_UNCOMPRESSED_FORMAT: c->raw_formats++; break;
  case SQ2_HFI_CAPABILITY: c->capabilities++; break;
  case SQ2_HFI_PROFILE_LEVEL: c->profile_levels++; break;
  case SQ2_HFI_BUFFER_ALLOC_MODE: c->alloc_modes++; break;
  default: break;
  }
 }
 return 0;
}
#endif
