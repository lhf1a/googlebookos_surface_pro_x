/* SPDX-License-Identifier: MIT */
/* Pure fail-closed predicate. No MMIO or IRQ invention. */
static inline bool sq2_el2_context_limit_allowed(bool enabled, bool el2,
        bool sc8180x, unsigned long long address, unsigned int banks,
        unsigned int irqs, unsigned int s2_banks)
{
    return enabled && el2 && sc8180x && address == 0x15000000ULL &&
           banks == 127 && irqs == 106 && s2_banks == 0;
}
