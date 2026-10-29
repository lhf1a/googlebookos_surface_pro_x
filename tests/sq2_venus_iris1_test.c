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
static struct { unsigned int addr; u32 value; } writes[5];
struct venus_core { unsigned char *wrapper_base, *cpu_cs_base; };
static void writel(u32 val, unsigned char *p)
{
 assert(n < 5);
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
 puts("IRIS1_MOCK_PASS: clock and DSP register addresses/order; 1024 masks preserve unrelated bits");
 return 0;
}
