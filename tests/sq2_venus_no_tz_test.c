/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
typedef uint32_t u32;
#define __iomem
#define BIT(n) (1U << (n))
static unsigned char memory[0x100000];
static u32 offsets[32], values[32];
static unsigned int writes;
static bool ignore_reset, bad_reset_read;
static unsigned int offset(void *p)
{
 unsigned int n = (unsigned char *)p - memory;
 assert(n < sizeof(memory) && !(n & 3));
 return n;
}
static u32 readl(void *p)
{
 unsigned int n = offset(p);
 u32 v;
 /* Forbidden threshold reads must fail the test, including indirect RMW. */
 assert(n < 0xe1020 || n > 0xe1034);
 if (n == 0xe3000 && bad_reset_read) return ~0U;
 memcpy(&v, p, 4);
 return v;
}
static void writel(u32 v, void *p)
{
 unsigned int n = offset(p);
 assert(writes < 32);
 offsets[writes] = n; values[writes++] = v;
 if (n == 0xe3000 && ignore_reset) return;
 memcpy(p, &v, 4);
}
static void udelay(unsigned int n) { assert(n == 1); }
#include "../probe/video/sq2-venus-no-tz.h"
static void setup(u32 reset)
{
 memset(memory, 0xa5, sizeof(memory));
 memcpy(memory + 0xe3000, &reset, 4);
 writes = 0; ignore_reset = bad_reset_read = false;
}
int main(void)
{
 const u32 order[] = {0xe1028,0xe102c,0xe1020,0xe1024,0xe1030,0xe1034,
  0xe1084,0xe3084,0xe3050,0xe3054,0xe3058,0xe306c,0xe3070,0xe3078,
  0xe3088,0xe308c,0xe304c,0xe2010,0xe2000,0xe3000};
 setup(0xa5a50010);
 assert(!sq2_no_tz_start_cpu(memory, 0x500000));
 assert(writes == 20 && !memcmp(offsets, order, sizeof(order)));
 assert(values[1] == 0x500000 && values[3] == 0x500000);
 assert(values[4] == 0x500000 && values[5] == 0x500000);
 assert(values[14] == 0xa5a5a5a4 && values[15] == 0xa5a5a5a4);
 assert(values[16] == 0xa5a5a5a5);
 assert(values[17] == (0xa5a5a5a5 & ~BIT(4)));
 assert(values[19] == 0xa5a50000);
 assert(!sq2_no_tz_stop_cpu(memory) && values[20] == 0xa5a50010);
 setup(0);
 assert(sq2_no_tz_start_cpu(memory, 0x500000) == -EBUSY && !writes);
 setup(0x10);
 assert(sq2_no_tz_start_cpu(memory, 0x600000) == -EINVAL && !writes);
 bad_reset_read = true;
 assert(sq2_no_tz_start_cpu(memory, 0x500000) == -EBUSY && !writes);
 assert(sq2_no_tz_stop_cpu(memory) == -EIO && !writes);
 setup(0x10); ignore_reset = true;
 assert(sq2_no_tz_start_cpu(memory, 0x500000) == -EIO);
 setup(0); ignore_reset = true;
 assert(sq2_no_tz_stop_cpu(memory) == -EBUSY && writes == 1);
 puts("SQ2_NO_TZ_PASS: ordered writes, preserved bits, no threshold reads, refusal and reset failures");
 return 0;
}
