/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../probe/video/sq2-venus-buffer.h"
int main(void)
{
 uint64_t n;
 assert(sq2_venus_buffer_valid(0x25800000,1));
 assert(sq2_venus_buffer_valid(0xbfffffff,1));
 assert(sq2_venus_buffer_valid(0x25800000,0x9a800000));
 assert(!sq2_venus_buffer_valid(0x257fffff,1));
 assert(!sq2_venus_buffer_valid(0xc0000000,1));
 assert(!sq2_venus_buffer_valid(0xbfffffff,2));
 assert(!sq2_venus_buffer_valid(0x25800000,0));
 assert(!sq2_venus_buffer_valid(0x125800000ULL,4096));
 assert(!sq2_venus_buffer_valid(UINT64_MAX,1));
 assert(!sq2_venus_buffer_valid(0x25800000,UINT64_MAX));
 for(n=1;n<=8192;n++) {
  assert(sq2_venus_buffer_valid(0xc0000000ULL-n,n));
  assert(!sq2_venus_buffer_valid(0xc0000000ULL-n,n+1));
  assert(sq2_venus_payload_valid(n,n,n));
  assert(sq2_venus_payload_valid(n,0,0));
  assert(!sq2_venus_payload_valid(n,n+1,0));
  assert(!sq2_venus_payload_valid(n,n,n+1));
 }
 puts("SQ2_VENUS_BUFFER_PASS: 10 address cases; 8192 boundary/payload lengths, six checks each; high address truncation and wrap rejected");
 return 0;
}
