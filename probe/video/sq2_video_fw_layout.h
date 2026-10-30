/* SPDX-License-Identifier: GPL-2.0-only */
/* The 6.3 MDT loader assumes program headers immediately follow ELF32_Ehdr.
 * Validate that assumption and every source/destination before calling it.
 * This deliberately accepts only complete, relocatable ARM ELF32 images.
 */
#ifndef SQ2_VIDEO_FW_LAYOUT_H
#define SQ2_VIDEO_FW_LAYOUT_H
#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stddef.h>
#include <stdint.h>
typedef uint32_t u32;
typedef uint64_t u64;
#endif

#define SQ2_FW_LIMIT (6U * 1024 * 1024)
#define SQ2_FW_MAX_SEGMENTS 32
struct sq2_fw_segment { u32 source, address, filesz, memsz; };
struct sq2_fw_layout {
 u32 base, span, count;
 struct sq2_fw_segment segments[SQ2_FW_MAX_SEGMENTS];
};

static u32 sq2_fw_u32(const unsigned char *p)
{
 return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static int sq2_fw_layout(const unsigned char *b, size_t size,
                        struct sq2_fw_layout *out)
{
 u32 i, j, count, base = 0xffffffffU;
 u64 end = 0;

 out->count = 0;
 out->base = out->span = 0;
 if (size < 52 || size > SQ2_FW_LIMIT || b[0] != 0x7f ||
     b[1] != 'E' || b[2] != 'L' || b[3] != 'F' ||
     b[4] != 1 || b[5] != 1 || b[6] != 1 ||
     b[18] != 40 || b[19] || sq2_fw_u32(b + 20) != 1 ||
     sq2_fw_u32(b + 28) != 52 || b[40] != 52 || b[41] ||
     b[42] != 32 || b[43])
  return -1;
 count = (u32)b[44] | (u32)b[45] << 8;
 if (!count || count > SQ2_FW_MAX_SEGMENTS || 52U + count * 32U > size)
  return -1;
 for (i = 0; i < count; i++) {
  const unsigned char *p = b + 52 + i * 32;
  u32 source = sq2_fw_u32(p + 4), addr = sq2_fw_u32(p + 12);
  u32 fs = sq2_fw_u32(p + 16), ms = sq2_fw_u32(p + 20);
  u32 flags = sq2_fw_u32(p + 24);
  struct sq2_fw_segment *s;

  if (sq2_fw_u32(p) != 1 || ((flags >> 24) & 7) == 2 || !ms)
   continue;
  if (!(flags & (1U << 27)) || fs > ms ||
      (u64)source + fs > size || (u64)addr + ms > 0xffffffffULL)
   return -1;
  for (j = 0; j < out->count; j++) {
   s = &out->segments[j];
   if ((u64)addr < (u64)s->address + s->memsz &&
       (u64)s->address < (u64)addr + ms)
    return -1;
  }
  s = &out->segments[out->count++];
  s->source = source; s->address = addr; s->filesz = fs; s->memsz = ms;
  if (addr < base)
   base = addr;
  if ((u64)addr + ms > end)
   end = (u64)addr + ms;
 }
 end = (end + 4095) & ~4095ULL;
 if (!out->count || (base & 4095) || end <= base || end - base > SQ2_FW_LIMIT)
  return -1;
 out->base = base;
 out->span = end - base;
 return 0;
}
#endif
