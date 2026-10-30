/* SPDX-License-Identifier: GPL-2.0-only */
/* Explicit laboratory match, not an alias for stock SC8180X or SM8150.
 * Clock/reset/ICC are owned by sq2-venus-power, not stock PM frequency tables.
 * HFI4 queries firmware for buffer requirements and codec capabilities.
 * Protection values exactly match the successful six-frame private test.
 */
static const struct venus_resources sq2_lab_res = {
 .iris1 = true,
 .sq2_windows_hfi = true,
 .hfi_version = HFI_VERSION_4XX,
 .dma_mask = DMA_BIT_MASK(32),
 .vmem_id = VIDC_RESOURCE_NONE,
 .cp_start = 0,
 .cp_size = 0x25800000,
 .cp_nonpixel_start = 0x1000000,
 .cp_nonpixel_size = 0x24800000,
 .fwname = "qcom/venus-5.2/venus.mbn",
};
