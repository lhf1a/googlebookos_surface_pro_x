#pragma once
/* Rebind only generated external-reference cells; validate all before writing. */
#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stddef.h>
#endif
static unsigned sq2_overlay_be32(const unsigned char *p) {
 return (unsigned)p[0]<<24 | (unsigned)p[1]<<16 | (unsigned)p[2]<<8 | p[3];
}
static int sq2_overlay_rebind(unsigned char *blob, size_t bytes,
 const unsigned *offsets, size_t count, unsigned original, unsigned live) {
 size_t i, j;
 if (!blob || !offsets || !count || !original || !live ||
     original == 0xffffffffu || live == 0xffffffffu) return -1;
 for (i=0; i<count; ++i) {
  size_t offset=offsets[i];
  if ((offset & 3u) || offset>bytes || bytes-offset<4 ||
      sq2_overlay_be32(blob+offset)!=original) return -1;
  for (j=0; j<i; ++j) if (offsets[j]==offsets[i]) return -1;
 }
 for (i=0; i<count; ++i) {
  unsigned char *p=blob+offsets[i];
  p[0]=(unsigned char)(live>>24); p[1]=(unsigned char)(live>>16);
  p[2]=(unsigned char)(live>>8); p[3]=(unsigned char)live;
 }
 return 0;
}
