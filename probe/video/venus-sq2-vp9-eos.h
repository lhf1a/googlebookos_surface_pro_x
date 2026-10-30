/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef VENUS_SQ2_VP9_EOS_H
#define VENUS_SQ2_VP9_EOS_H

/* SQ2 firmware returns a successful, empty VP9 drain marker with DROP_FRAME.
 * Observed packet: error=0, bytes=offset=0, EOS|SYNCFRAME|DROP_FRAME.
 * Normalize only this exact marker; preserve every other error/flag case.
 * Caller must identify the SQ2 VP9 decoder, never an encoder or other SoC.
 */
static inline unsigned int
venus_sq2_vp9_eos_flags(int sq2_vp9_decoder, unsigned int error,
                       unsigned int bytes, unsigned int offset,
                       unsigned int flags)
{
 if (sq2_vp9_decoder && !error && !bytes && !offset &&
     flags == (HFI_BUFFERFLAG_EOS | HFI_BUFFERFLAG_SYNCFRAME |
               HFI_BUFFERFLAG_DROP_FRAME))
  return flags & ~HFI_BUFFERFLAG_DROP_FRAME;
 return flags;
}
#endif
