/* SPDX-License-Identifier: GPL-2.0-only */
/* Mock MMIO test of the same helper compiled into the candidate driver.
 * Addresses/bit semantics independently taken from the fixed downstream tree.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef uint32_t u32;
#define BIT(n) (1U << (n))
#include "hfi_venus_io.h"
static unsigned char regs[0x100000];
static unsigned int n;
static struct { unsigned int addr; u32 value; } writes[6];
struct venus_core { unsigned char *wrapper_base, *cpu_cs_base; };
static void writel(u32 val, unsigned char *p)
{
 assert(n < 6);
 writes[n].addr = p - regs;
 writes[n++].value = val;
}
#include "venus-iris1.h"
int main(void)
{
 struct venus_core core = {regs + 0xe0000, regs + 0xd2000};
 u32 mask;
 venus_iris1_clock_config(&core);
 venus_iris1_dsp_map(&core, 0x25800000, 0x100000);
 assert(n == 5);
 assert(writes[0].addr == 0xe2010 && writes[0].value == 0);
 assert(writes[1].addr == 0xe2000 && writes[1].value == 0);
 assert(writes[2].addr == 0xd2034 && writes[2].value == 0x25800000);
 assert(writes[3].addr == 0xd2038 && writes[3].value == 0x25800000);
 assert(writes[4].addr == 0xd203c && writes[4].value == 0x100000);
 assert(venus_iris1_irq_mask(0x1f6) == 0x1e2);
 assert(venus_iris1_irq_mask(0xffffffff) == 0xffffffeb);
 for (mask = 0; mask < 1024; mask++)
  assert(venus_iris1_irq_mask(mask) == (mask & ~0x14U));
 n = 0;
 venus_sq2_cpu_map(&core, 0xbf000000, 0xbf259000);
 assert(n == 6);
 assert(writes[0].addr == 0xd2064 && writes[0].value == 0xbf000000);
 assert(writes[1].addr == 0xd2068 && writes[1].value == 0x1000000);
 assert(writes[2].addr == 0xd2054 && writes[2].value == 0xbf000000);
 assert(writes[3].addr == 0xd2050 && writes[3].value == 1);
 assert(writes[4].addr == 0xd205c && writes[4].value == 0xbf259000);
 assert(writes[5].addr == 0xd2060 && writes[5].value == 0);
 assert(venus_sq2_uc_valid(0xbf000000, 0x1000000));
 assert(venus_sq2_uc_valid(0x25800000, 0x1000000));
 assert(!venus_sq2_uc_valid(0xbf000001, 0x1000000));
 assert(!venus_sq2_uc_valid(0x257fffff, 0x1000000));
 assert(!venus_sq2_uc_valid(0x100000000ULL, 0x1000000));
 assert(!venus_sq2_uc_valid(0xffffffffffffffffULL, 0x1000000));
 assert(!venus_sq2_uc_valid(0xbf000000, 0));
 puts("SQ2_UC_MOCK_PASS: six CPU-only writes; 7 aperture/size boundary checks");
 puts("IRIS1_MOCK_PASS: clock and DSP register addresses/order; 1024 masks preserve unrelated bits");
 return 0;
}
