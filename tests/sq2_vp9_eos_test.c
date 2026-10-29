#include <assert.h>
#include <stdio.h>
#define HFI_BUFFERFLAG_EOS 0x00000001u
#define HFI_BUFFERFLAG_SYNCFRAME 0x00000020u
#define HFI_BUFFERFLAG_DROP_FRAME 0x20000000u
#include "venus-sq2-vp9-eos.h"

int main(void)
{
 const unsigned int marker = 0x20000021u;
 assert(venus_sq2_vp9_eos_flags(1, 0, 0, 0, marker) == 0x21u);
 assert(venus_sq2_vp9_eos_flags(0, 0, 0, 0, marker) == marker);
 /* All single-bit errors, lengths, offsets and flag deviations stay intact.
  * This includes DATACORRUPT, missing EOS, and actual dropped frame data. */
 for (unsigned int bit = 0; bit < 32; bit++) {
  unsigned int v = 1u << bit;
  assert(venus_sq2_vp9_eos_flags(1, v, 0, 0, marker) == marker);
  assert(venus_sq2_vp9_eos_flags(1, 0, v, 0, marker) == marker);
  assert(venus_sq2_vp9_eos_flags(1, 0, 0, v, marker) == marker);
  assert(venus_sq2_vp9_eos_flags(1, 0, 0, 0, marker ^ v) == (marker ^ v));
 }
 puts("SQ2 VP9 EOS marker and error preservation passed");
 return 0;
}
