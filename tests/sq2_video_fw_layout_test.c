/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../probe/video/sq2_video_fw_layout.h"

static unsigned char image[4096];
static struct sq2_fw_layout layout;
static void put32(unsigned char *p, u32 v)
{
 p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}
static void segment(unsigned int n, u32 addr, u32 ms)
{
 unsigned char *p = image + 52 + n * 32;
 put32(p, 1); put32(p + 4, 512); put32(p + 12, addr);
 put32(p + 16, 16); put32(p + 20, ms); put32(p + 24, 1U << 27);
}
static void reset(void)
{
 memset(image, 0, sizeof(image));
 memcpy(image, "\177ELF\1\1\1", 7);
 image[18] = 40; put32(image + 20, 1); put32(image + 28, 52);
 image[40] = 52; image[42] = 32; image[44] = 2;
 segment(0, 0x0f500000, 4096); segment(1, 0x0f502000, 8192);
}
#define BAD() assert(sq2_fw_layout(image, sizeof(image), &layout) < 0)
int main(void)
{
 unsigned int n;
 reset();
 assert(!sq2_fw_layout(image, sizeof(image), &layout));
 assert(layout.count == 2 && layout.base == 0x0f500000 && layout.span == 16384);
 /* Every truncated header/table, including zero bytes. */
 for (n = 0; n < 116; n++)
  assert(sq2_fw_layout(image, n, &layout) < 0);
 reset(); image[5] = 2; BAD();
 reset(); image[4] = 2; BAD();
 reset(); image[18] = 183; BAD();
 reset(); put32(image + 28, 64); BAD();
 reset(); image[42] = 56; BAD();
 reset(); image[44] = 33; BAD();
 reset(); put32(image + 52 + 4, 4090); BAD();
 reset(); put32(image + 52 + 4, 0xfffffff8); BAD();
 reset(); put32(image + 52 + 20, 8); BAD();
 reset(); put32(image + 52 + 24, 0); BAD();
 reset(); segment(1, 0x0f500800, 4096); BAD();
 reset(); segment(1, 0xfffff000, 8192); BAD();
 reset(); segment(1, 0x1f500000, 8192); BAD();
 reset(); segment(0, 0x0f500001, 4096); BAD();
 reset(); put32(image + 52 + 24, 2U << 24);
 assert(!sq2_fw_layout(image, sizeof(image), &layout) && layout.count == 1);
 reset(); put32(image + 52, 0); put32(image + 84, 0); BAD();
 /* A pure BSS segment remains valid; nothing is read from its source. */
 reset(); put32(image + 52 + 16, 0);
 assert(!sq2_fw_layout(image, sizeof(image), &layout));
 puts("SQ2_FW_LAYOUT_PASS: valid relocation/BSS/hash exclusion, truncation, overlap, overflow, nonrelocatable and oversize rejected");
 return 0;
}
