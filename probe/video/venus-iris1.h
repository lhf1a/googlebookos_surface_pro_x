/* SPDX-License-Identifier: GPL-2.0-only */
/* Iris1/VPU5 register differences, using HFI 4xx messages.
 * Reference: OnePlus SM8150 32f26bf3, venus_hfi.c/vidc_hfi_io.h.
 * core.h and hfi_venus_io.h must precede this header.
 */
#ifndef SQ2_VENUS_IRIS1_H
#define SQ2_VENUS_IRIS1_H

/* Explicit SQ2 Windows-reference profile; never implied by HFI4 alone. */
#define SQ2_VENUS_UC_BYTES 0x01000000U
static inline int venus_sq2_uc_valid(unsigned long long addr, u32 size)
{
 return size == SQ2_VENUS_UC_BYTES && addr >= 0x25800000ULL &&
        addr <= 0xc0000000ULL - size;
}

static inline void venus_sq2_cpu_map(struct venus_core *core, u32 addr,
                                    u32 sfr)
{
 writel(addr, core->cpu_cs_base + 0x64);
 writel(SQ2_VENUS_UC_BYTES, core->cpu_cs_base + 0x68);
 writel(addr, core->cpu_cs_base + 0x54);
 writel(1, core->cpu_cs_base + 0x50);
 writel(sfr, core->cpu_cs_base + 0x5c);
 writel(0, core->cpu_cs_base + 0x60);
}

static inline void venus_iris1_clock_config(struct venus_core *core)
{
 writel(0, core->wrapper_base + WRAPPER_CPU_CGC_DIS);
 writel(0, core->wrapper_base + WRAPPER_CPU_CLOCK_CONFIG);
}

static inline void venus_iris1_dsp_map(struct venus_core *core, u32 addr, u32 size)
{
 /* No CVP domain: the downstream VPU5 path mirrors the CPU interface queues. */
 writel(addr, core->cpu_cs_base + 0x34); /* VMIMSG: DSP QTBL */
 writel(addr, core->cpu_cs_base + 0x38); /* VMIMSGAG0: DSP UC address */
 writel(size, core->cpu_cs_base + 0x3c); /* VMIMSGAG1: DSP UC size */
}

static inline u32 venus_iris1_irq_mask(u32 mask)
{
 return mask & ~(WRAPPER_INTR_MASK_A2HWD_BASK | WRAPPER_INTR_MASK_A2HCPU_MASK);
}
#endif
